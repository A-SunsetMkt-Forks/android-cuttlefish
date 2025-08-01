/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/host/libs/vm_manager/qemu_manager.h"

#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <android-base/strings.h>
#include <android-base/logging.h>
#include <vulkan/vulkan.h>

#include "cuttlefish/common/libs/utils/architecture.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/common/libs/utils/wait_for_unix_socket.h"
#include "cuttlefish/host/libs/config/config_constants.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/external_network_mode.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/host/libs/vm_manager/vhost_user.h"

// This is the QEMU default, but set it explicitly just in case it
// changes upstream
static const int kMaxSerialPorts = 31;

namespace cuttlefish {
namespace vm_manager {
namespace {

std::string GetMonitorPath(const CuttlefishConfig& config) {
  return config.ForDefaultInstance().PerInstanceInternalUdsPath(
      "qemu_monitor.sock");
}

StopperResult Stop() {
  auto config = CuttlefishConfig::Get();
  auto monitor_path = GetMonitorPath(*config);
  auto monitor_sock = SharedFD::SocketLocalClient(
      monitor_path.c_str(), false, SOCK_STREAM);

  if (!monitor_sock->IsOpen()) {
    LOG(ERROR) << "The connection to qemu is closed, is it still running?";
    return StopperResult::kStopFailure;
  }
  char msg[] = "{\"execute\":\"qmp_capabilities\"}{\"execute\":\"quit\"}";
  ssize_t len = sizeof(msg) - 1;
  while (len > 0) {
    int tmp = monitor_sock->Write(msg, len);
    if (tmp < 0) {
      LOG(ERROR) << "Error writing to socket: " << monitor_sock->StrError();
      return StopperResult::kStopFailure;
    }
    len -= tmp;
  }
  // Log the reply
  char buff[1000];
  while ((len = monitor_sock->Read(buff, sizeof(buff) - 1)) > 0) {
    buff[len] = '\0';
    LOG(INFO) << "From qemu monitor: " << buff;
  }

  return StopperResult::kStopSuccess;
}

Result<std::pair<int, int>> GetQemuVersion(const std::string& qemu_binary) {
  Command qemu_version_cmd(qemu_binary);
  qemu_version_cmd.AddParameter("-version");

  std::string qemu_version_output =
      CF_EXPECT(RunAndCaptureStdout(std::move(qemu_version_cmd)));

  // Snip around the extra text we don't care about
  qemu_version_output.erase(0, std::string("QEMU emulator version ").length());
  auto space_pos = qemu_version_output.find(" ", 0);
  if (space_pos != std::string::npos) {
    qemu_version_output.resize(space_pos);
  }

  auto qemu_version_bits = android::base::Split(qemu_version_output, ".");
  return {{std::stoi(qemu_version_bits[0]), std::stoi(qemu_version_bits[1])}};
}

}  // namespace

QemuManager::QemuManager(Arch arch) : arch_(arch) {}

bool QemuManager::IsSupported() {
  return HostSupportsQemuCli();
}

Result<std::unordered_map<std::string, std::string>>
QemuManager::ConfigureGraphics(
    const CuttlefishConfig::InstanceSpecific& instance) {
  // Override the default HAL search paths in all cases. We do this because
  // the HAL search path allows for fallbacks, and fallbacks in conjunction
  // with properties lead to non-deterministic behavior while loading the
  // HALs.

  std::unordered_map<std::string, std::string> bootconfig_args;
  auto gpu_mode = instance.gpu_mode();
  if (gpu_mode == kGpuModeGuestSwiftshader) {
    bootconfig_args = {
        {"androidboot.cpuvulkan.version", std::to_string(VK_API_VERSION_1_2)},
        {"androidboot.hardware.gralloc", "minigbm"},
        {"androidboot.hardware.hwcomposer", instance.hwcomposer()},
        {"androidboot.hardware.hwcomposer.display_finder_mode", "drm"},
        {"androidboot.hardware.hwcomposer.display_framebuffer_format",
         instance.guest_uses_bgra_framebuffers() ? "bgra" : "rgba"},
        {"androidboot.hardware.egl", "angle"},
        {"androidboot.hardware.vulkan", "pastel"},
        // OpenGL ES 3.1
        {"androidboot.opengles.version", "196609"},
    };
  } else if (gpu_mode == kGpuModeDrmVirgl) {
    bootconfig_args = {
        {"androidboot.cpuvulkan.version", "0"},
        {"androidboot.hardware.gralloc", "minigbm"},
        {"androidboot.hardware.hwcomposer", "ranchu"},
        {"androidboot.hardware.hwcomposer.mode", "client"},
        {"androidboot.hardware.hwcomposer.display_finder_mode", "drm"},
        {"androidboot.hardware.hwcomposer.display_framebuffer_format",
         instance.guest_uses_bgra_framebuffers() ? "bgra" : "rgba"},
        {"androidboot.hardware.egl", "mesa"},
        // No "hardware" Vulkan support, yet
        // OpenGL ES 3.0
        {"androidboot.opengles.version", "196608"},
    };
  } else if (gpu_mode == kGpuModeGfxstream ||
             gpu_mode == kGpuModeGfxstreamGuestAngle ||
             gpu_mode == kGpuModeGfxstreamGuestAngleHostSwiftShader ||
             gpu_mode == kGpuModeGfxstreamGuestAngleHostLavapipe) {
    const bool uses_angle =
        gpu_mode == kGpuModeGfxstreamGuestAngle ||
        gpu_mode == kGpuModeGfxstreamGuestAngleHostSwiftShader ||
        gpu_mode == kGpuModeGfxstreamGuestAngleHostLavapipe;
    const std::string gles_impl = uses_angle ? "angle" : "emulation";
    const std::string gltransport =
        (instance.guest_android_version() == "11.0.0") ? "virtio-gpu-pipe"
                                                       : "virtio-gpu-asg";
    bootconfig_args = {
        {"androidboot.cpuvulkan.version", "0"},
        {"androidboot.hardware.gralloc", "minigbm"},
        {"androidboot.hardware.hwcomposer", instance.hwcomposer()},
        {"androidboot.hardware.hwcomposer.display_finder_mode", "drm"},
        {"androidboot.hardware.hwcomposer.display_framebuffer_format",
         instance.guest_uses_bgra_framebuffers() ? "bgra" : "rgba"},
        {"androidboot.hardware.egl", gles_impl},
        {"androidboot.hardware.vulkan", "ranchu"},
        {"androidboot.hardware.gltransport", gltransport},
        {"androidboot.opengles.version", "196609"},  // OpenGL ES 3.1
    };
  } else if (instance.gpu_mode() == kGpuModeNone) {
    return {};
  } else {
    return CF_ERR("Unhandled GPU mode: " << instance.gpu_mode());
  }

  if (!instance.gpu_angle_feature_overrides_enabled().empty()) {
    bootconfig_args["androidboot.hardware.angle_feature_overrides_enabled"] =
        instance.gpu_angle_feature_overrides_enabled();
  }
  if (!instance.gpu_angle_feature_overrides_disabled().empty()) {
    bootconfig_args["androidboot.hardware.angle_feature_overrides_disabled"] =
        instance.gpu_angle_feature_overrides_disabled();
  }

  return bootconfig_args;
}

Result<std::unordered_map<std::string, std::string>>
QemuManager::ConfigureBootDevices(
    const CuttlefishConfig::InstanceSpecific& instance) {
  const int num_disks = instance.virtual_disk_paths().size();
  const int num_gpu = instance.hwcomposer() != kHwComposerNone;
  switch (arch_) {
    case Arch::Arm:
      return {{{"androidboot.boot_devices", "3f000000.pcie"}}};
    case Arch::Arm64:
#ifdef __APPLE__
      return {{{"androidboot.boot_devices", "3f000000.pcie"}}};
#else
      return {{{"androidboot.boot_devices", "4010000000.pcie"}}};
#endif
    case Arch::RiscV64:
      return {{{"androidboot.boot_devices", "soc/30000000.pci"}}};
    case Arch::X86:
    case Arch::X86_64: {
      // QEMU has additional PCI devices for an ISA bridge and PIIX4
      // virtio_gpu precedes the first console or disk
      // TODO(schuffelen): Simplify this logic when crosvm uses multiport
      int pci_offset = 3 + num_gpu - VmManager::kDefaultNumHvcs;
      return ConfigureMultipleBootDevices("pci0000:00/0000:00:", pci_offset,
                                          num_disks);
    }
  }
}

Result<std::vector<MonitorCommand>> QemuManager::StartCommands(
    const CuttlefishConfig& config,
    std::vector<VmmDependencyCommand*>& dependency_commands) {
  std::vector<MonitorCommand> commands;
  auto instance = config.ForDefaultInstance();
  std::string qemu_binary = instance.qemu_binary_dir();
  switch (arch_) {
    case Arch::Arm:
      qemu_binary += "/qemu-system-arm";
      break;
    case Arch::Arm64:
      qemu_binary += "/qemu-system-aarch64";
      break;
    case Arch::RiscV64:
      qemu_binary += "/qemu-system-riscv64";
      break;
    case Arch::X86:
      qemu_binary += "/qemu-system-i386";
      break;
    case Arch::X86_64:
      qemu_binary += "/qemu-system-x86_64";
      break;
  }

  auto qemu_version = CF_EXPECT(GetQemuVersion(qemu_binary));
  Command qemu_cmd(qemu_binary, KillSubprocessFallback(Stop));

  qemu_cmd.AddPrerequisite([&dependency_commands]() -> Result<void> {
    for (auto dependencyCommand : dependency_commands) {
      CF_EXPECT(dependencyCommand->WaitForAvailability());
    }

    return {};
  });

  int hvc_num = 0;
  int serial_num = 0;
  auto add_hvc_sink = [&qemu_cmd, &hvc_num]() {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("null,id=hvc", hvc_num);
    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter("virtconsole,bus=virtio-serial.0,chardev=hvc",
                          hvc_num);
    hvc_num++;
  };
  auto add_serial_sink = [&qemu_cmd, &serial_num]() {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("null,id=serial", serial_num);
    qemu_cmd.AddParameter("-serial");
    qemu_cmd.AddParameter("chardev:serial", serial_num);
    serial_num++;
  };
  auto add_serial_console_ro = [&qemu_cmd,
                                &serial_num](const std::string& output) {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("file,id=serial", serial_num, ",path=", output,
                          ",append=on");
    qemu_cmd.AddParameter("-serial");
    qemu_cmd.AddParameter("chardev:serial", serial_num);
    serial_num++;
  };
  auto add_serial_console = [&qemu_cmd,
                             &serial_num](const std::string& prefix) {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("pipe,id=serial", serial_num, ",path=", prefix);
    qemu_cmd.AddParameter("-serial");
    qemu_cmd.AddParameter("chardev:serial", serial_num);
    serial_num++;
  };
  auto add_hvc_ro = [&qemu_cmd, &hvc_num](const std::string& output) {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("file,id=hvc", hvc_num, ",path=", output,
                          ",append=on");
    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter("virtconsole,bus=virtio-serial.0,chardev=hvc",
                          hvc_num);
    hvc_num++;
  };
  auto add_hvc = [&qemu_cmd, &hvc_num](const std::string& prefix) {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("pipe,id=hvc", hvc_num, ",path=", prefix);
    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter("virtconsole,bus=virtio-serial.0,chardev=hvc",
                          hvc_num);
    hvc_num++;
  };
  auto add_hvc_serial = [&qemu_cmd, &hvc_num](const std::string& prefix) {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("serial,id=hvc", hvc_num, ",path=", prefix);
    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter("virtconsole,bus=virtio-serial.0,chardev=hvc",
                          hvc_num);
    hvc_num++;
  };

  bool is_arm = arch_ == Arch::Arm || arch_ == Arch::Arm64;
  bool is_x86 = arch_ == Arch::X86 || arch_ == Arch::X86_64;
  bool is_riscv64 = arch_ == Arch::RiscV64;

  auto access_kregistry_size_bytes = 0;
  if (FileExists(instance.access_kregistry_path())) {
    access_kregistry_size_bytes = FileSize(instance.access_kregistry_path());
    CF_EXPECT((access_kregistry_size_bytes & (1024 * 1024 - 1)) == 0,
              instance.access_kregistry_path()
                  << " file size (" << access_kregistry_size_bytes
                  << ") not a multiple of 1MB");
  }

  auto hwcomposer_pmem_size_bytes = 0;
  if (instance.hwcomposer() != kHwComposerNone) {
    if (FileExists(instance.hwcomposer_pmem_path())) {
      hwcomposer_pmem_size_bytes = FileSize(instance.hwcomposer_pmem_path());
      CF_EXPECT((hwcomposer_pmem_size_bytes & (1024 * 1024 - 1)) == 0,
                instance.hwcomposer_pmem_path()
                    << " file size (" << hwcomposer_pmem_size_bytes
                    << ") not a multiple of 1MB");
    }
  }

  auto pstore_size_bytes = 0;
  if (FileExists(instance.pstore_path())) {
    pstore_size_bytes = FileSize(instance.pstore_path());
    CF_EXPECT((pstore_size_bytes & (1024 * 1024 - 1)) == 0,
              instance.pstore_path() << " file size (" << pstore_size_bytes
                                     << ") not a multiple of 1MB");
  }

  qemu_cmd.AddParameter("-name");
  qemu_cmd.AddParameter("guest=", instance.instance_name(), ",debug-threads=on");

  std::string machine = is_x86 ? "pc,nvdimm=on" : "virt";
  if (is_arm) {
    if (IsHostCompatible(arch_)) {
      machine += ",gic-version=3";
    } else {
      // QEMU doesn't support GICv3 with TCG yet
      machine += ",gic-version=2";
      CF_EXPECT(instance.cpus() <= 8, "CPUs must be no more than 8 with GICv2");
    }
  }
  if (instance.mte()) {
    machine += ",mte=on";
  }
  qemu_cmd.AddParameter("-machine");
  qemu_cmd.AddParameter(machine, ",usb=off,dump-guest-core=off,memory-backend=vm_ram");

  if (IsHostCompatible(arch_)) {
    qemu_cmd.AddParameter("-accel");
    std::string accel;
#ifdef __linux__
    accel = "kvm";
    if (!config.kvm_path().empty()) {
      accel += ",device=" + config.kvm_path();
    }
#elif defined(__APPLE__)
    accel = "hvf";
#else
#error "Unknown OS"
#endif
    qemu_cmd.AddParameter(accel);
  }

  // Memory must be backed by a file for vhost-user to work correctly, otherwise
  // qemu doesn't send the memory mappings necessary for the backend to access
  // the virtqueues.
  qemu_cmd.AddParameter("-object");
  qemu_cmd.AddParameter("memory-backend-file,size=", instance.memory_mb(), "M",
                        ",prealloc=on,share=on,mem-path=",
                        instance.PerInstanceInternalPath("qemu.mem"),
                        ",id=vm_ram");

  qemu_cmd.AddParameter("-m");
  auto maxmem = instance.memory_mb() +
                (access_kregistry_size_bytes / 1024 / 1024) +
                (hwcomposer_pmem_size_bytes / 1024 / 1024) +
                (is_x86 ? pstore_size_bytes / 1024 / 1024 : 0);
  auto slots = is_x86 ? ",slots=2" : "";
  qemu_cmd.AddParameter("size=", instance.memory_mb(), "M",
                        ",maxmem=", maxmem, "M", slots);

  qemu_cmd.AddParameter("-overcommit");
  qemu_cmd.AddParameter("mem-lock=off");

  // Assume SMT is always 2 threads per core, which is how most hardware
  // today is configured, and the way crosvm does it
  qemu_cmd.AddParameter("-smp");
  if (instance.smt()) {
    CF_EXPECT(instance.cpus() % 2 == 0,
              "CPUs must be a multiple of 2 in SMT mode");
    qemu_cmd.AddParameter(instance.cpus(), ",cores=",
                          instance.cpus() / 2, ",threads=2");
  } else {
    qemu_cmd.AddParameter(instance.cpus(), ",cores=",
                          instance.cpus(), ",threads=1");
  }

  qemu_cmd.AddParameter("-uuid");
  qemu_cmd.AddParameter(instance.uuid());

  qemu_cmd.AddParameter("-no-user-config");
  qemu_cmd.AddParameter("-nodefaults");
  qemu_cmd.AddParameter("-no-shutdown");

  qemu_cmd.AddParameter("-rtc");
  qemu_cmd.AddParameter("base=utc");

  qemu_cmd.AddParameter("-boot");
  qemu_cmd.AddParameter("strict=on");

  qemu_cmd.AddParameter("-chardev");
  qemu_cmd.AddParameter("socket,id=charmonitor,path=", GetMonitorPath(config),
                        ",server=on,wait=off");

  qemu_cmd.AddParameter("-mon");
  qemu_cmd.AddParameter("chardev=charmonitor,id=monitor,mode=control");

  auto gpu_mode = instance.gpu_mode();
  if (gpu_mode == kGpuModeDrmVirgl) {
    qemu_cmd.AddParameter("-display");
    qemu_cmd.AddParameter("egl-headless");

    qemu_cmd.AddParameter("-vnc");
    qemu_cmd.AddParameter("127.0.0.1:", instance.qemu_vnc_server_port());
  } else if (gpu_mode == kGpuModeGuestSwiftshader ||
             gpu_mode == kGpuModeGfxstream ||
             gpu_mode == kGpuModeGfxstreamGuestAngle ||
             gpu_mode == kGpuModeGfxstreamGuestAngleHostSwiftShader ||
             gpu_mode == kGpuModeGfxstreamGuestAngleHostLavapipe) {
    qemu_cmd.AddParameter("-vnc");
    qemu_cmd.AddParameter("127.0.0.1:", instance.qemu_vnc_server_port());
  } else {
    qemu_cmd.AddParameter("-display");
    qemu_cmd.AddParameter("none");
  }

  if (instance.hwcomposer() != kHwComposerNone) {
    auto display_configs = instance.display_configs();
    CF_EXPECT(!display_configs.empty());
    auto display_config = display_configs[0];

    qemu_cmd.AddParameter("-device");

    std::string gpu_device;
    if (gpu_mode == kGpuModeGuestSwiftshader || qemu_version.first < 6) {
      gpu_device = "virtio-gpu-pci";
    } else if (gpu_mode == kGpuModeDrmVirgl) {
      gpu_device = "virtio-gpu-gl-pci";
    } else if (gpu_mode == kGpuModeGfxstream) {
      gpu_device =
          "virtio-gpu-rutabaga,x-gfxstream-gles=on,gfxstream-vulkan=on,"
          "x-gfxstream-composer=on,hostmem=256M";
    } else if (gpu_mode == kGpuModeGfxstreamGuestAngle ||
               gpu_mode == kGpuModeGfxstreamGuestAngleHostSwiftShader ||
               gpu_mode == kGpuModeGfxstreamGuestAngleHostLavapipe) {
      gpu_device =
          "virtio-gpu-rutabaga,gfxstream-vulkan=on,"
          "x-gfxstream-composer=on,hostmem=256M";

      if (gpu_mode == kGpuModeGfxstreamGuestAngleHostSwiftShader) {
        // See https://github.com/KhronosGroup/Vulkan-Loader.
        const std::string swiftshader_icd_json =
            HostUsrSharePath("vulkan/icd.d/vk_swiftshader_icd.json");
        qemu_cmd.AddEnvironmentVariable("VK_DRIVER_FILES",
                                        swiftshader_icd_json);
        qemu_cmd.AddEnvironmentVariable("VK_ICD_FILENAMES",
                                        swiftshader_icd_json);
      } else if (gpu_mode == kGpuModeGfxstreamGuestAngleHostLavapipe) {
        // See https://github.com/KhronosGroup/Vulkan-Loader.
        const std::string lavapipe_icd_json =
            HostUsrSharePath("vulkan/icd.d/vk_lavapipe_icd.cf.json");
        qemu_cmd.AddEnvironmentVariable("VK_DRIVER_FILES", lavapipe_icd_json);
        qemu_cmd.AddEnvironmentVariable("VK_ICD_FILENAMES", lavapipe_icd_json);
      }
    }

    qemu_cmd.AddParameter(
        gpu_device, ",id=gpu0",
        fmt::format(",addr={:0>2x}.0", VmManager::kGpuPciSlotNum),
        ",xres=", display_config.width, ",yres=", display_config.height);
  }

  if (!instance.console()) {
    // In kgdb mode, earlycon is an interactive console, and so early
    // dmesg will go there instead of the kernel.log. On QEMU, we do this
    // bit of logic up before the hvc console is set up, so the command line
    // flags appear in the right order and "append=on" does the right thing
    if (instance.enable_kernel_log() &&
        (instance.kgdb() || instance.use_bootloader())) {
      add_serial_console_ro(instance.kernel_log_pipe_name());
    }
  }

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter(
      "virtio-serial-pci-non-transitional,max_ports=", kMaxSerialPorts,
      ",id=virtio-serial");

  // /dev/hvc0 = kernel console
  // If kernel log is enabled, the virtio-console port will be specified as
  // a true console for Linux, and kernel messages will be printed there.
  // Otherwise, the port will still be set up for bootloader and userspace
  // messages, but the kernel will not print anything here. This keeps our
  // kernel log event features working. If an alternative "earlycon" boot
  // console is configured above on a legacy serial port, it will control
  // the main log until the virtio-console takes over.
  // (Note that QEMU does not automatically generate console= parameters for
  //  the bootloader/kernel cmdline, so the control of whether this pipe is
  //  actually managed by the kernel as a console is handled elsewhere.)
  add_hvc_ro(instance.kernel_log_pipe_name());

  // /dev/hvc1 = serial console
  if (instance.console()) {
    if (instance.kgdb() || instance.use_bootloader()) {
      add_serial_console(instance.console_pipe_prefix());

      // In kgdb mode, we have the interactive console on ttyS0 (both Android's
      // console and kdb), so we can disable the virtio-console port usually
      // allocated to Android's serial console, and redirect it to a sink. This
      // ensures that that the PCI device assignments (and thus sepolicy) don't
      // have to change
      add_hvc_sink();
    } else {
      add_serial_sink();
      add_hvc(instance.console_pipe_prefix());
    }
  } else {
    if (instance.kgdb() || instance.use_bootloader()) {
      // The add_serial_console_ro() call above was applied by the time we reach
      // this code, so we don't need another add_serial_*() call
    }

    // as above, create a fake virtio-console 'sink' port when the serial
    // console is disabled, so the PCI device ID assignments don't move
    // around
    add_hvc_sink();
  }

  // /dev/hvc2 = serial logging
  // Serial port for logcat, redirected to a pipe
  add_hvc_ro(instance.logcat_pipe_name());

  // /dev/hvc3 = keymaster (C++ implementation)
  add_hvc(instance.PerInstanceInternalPath("keymaster_fifo_vm"));
  // /dev/hvc4 = gatekeeper
  add_hvc(instance.PerInstanceInternalPath("gatekeeper_fifo_vm"));
  // /dev/hvc5 = bt
  if (config.enable_host_bluetooth()) {
    add_hvc(instance.PerInstanceInternalPath("bt_fifo_vm"));
  } else {
    add_hvc_sink();
  }

  // /dev/hvc6 = gnss
  // /dev/hvc7 = location
  if (instance.enable_gnss_grpc_proxy()) {
    add_hvc(instance.PerInstanceInternalPath("gnsshvc_fifo_vm"));
    add_hvc(instance.PerInstanceInternalPath("locationhvc_fifo_vm"));
  } else {
    for (auto i = 0; i < 2; i++) {
      add_hvc_sink();
    }
  }

  /* Added one for confirmation UI.
   *
   * b/237452165
   *
   * Confirmation UI is not supported with QEMU for now. In order
   * to not conflict with confirmation UI-related configurations used
   * w/ Crosvm, we should add one generic avc.
   *
   * confui_fifo_vm.{in/out} are created along with the streamer process,
   * which is not created w/ QEMU.
   */
  // /dev/hvc8 = confirmationui
  add_hvc_sink();

  // /dev/hvc9 = uwb
  if (config.enable_host_uwb()) {
    add_hvc(instance.PerInstanceInternalPath("uwb_fifo_vm"));
  } else {
    add_hvc_sink();
  }

  // /dev/hvc10 = oemlock
  add_hvc(instance.PerInstanceInternalPath("oemlock_fifo_vm"));

  // /dev/hvc11 = keymint (Rust implementation)
  add_hvc(instance.PerInstanceInternalPath("keymint_fifo_vm"));

  // /dev/hvc12 = nfc
  if (config.enable_host_nfc()) {
    add_hvc(instance.PerInstanceInternalPath("nfc_fifo_vm"));
  } else {
    add_hvc_sink();
  }

  // /dev/hvc13 = sensors
  add_hvc(instance.PerInstanceInternalPath("sensors_fifo_vm"));

  // /dev/hvc14 = MCU CONTROL
  if (instance.mcu()["control"]["type"].asString() == "serial") {
    auto path = instance.PerInstanceInternalPath("mcu");
    path += "/" + instance.mcu()["control"]["path"].asString();
    add_hvc_serial(path);
  } else {
    add_hvc_sink();
  }

  // /dev/hvc15 = MCU UART
  if (instance.mcu()["uart0"]["type"].asString() == "serial") {
    auto path = instance.PerInstanceInternalPath("mcu");
    path += "/" + instance.mcu()["uart0"]["path"].asString();
    add_hvc_serial(path);
  } else {
    add_hvc_sink();
  }

  // /dev/hvc16 = Ti50 TPM FIFO
  if (!instance.ti50_emulator().empty()) {
    // TODO
    // add_hvc_socket(instance.PerInstancePath("direct_tpm_fifo"));
    add_hvc_sink();
  } else {
    add_hvc_sink();
  }

  if (instance.enable_jcard_simulator()) {
    // /dev/hvc17 = keymint (jcardsim implementation)
    add_hvc(instance.PerInstanceInternalPath("jcardsim_fifo_vm"));
  } else {
    add_hvc_sink();
  }

  auto disk_num = instance.virtual_disk_paths().size();

  for (auto i = 0; i < VmManager::kMaxDisks - disk_num; i++) {
    add_hvc_sink();
  }

  CF_EXPECT(
      hvc_num + disk_num == VmManager::kMaxDisks + VmManager::kDefaultNumHvcs,
      "HVC count (" << hvc_num << ") + disk count (" << disk_num << ") "
                    << "is not the expected total of "
                    << VmManager::kMaxDisks + VmManager::kDefaultNumHvcs
                    << " devices");

  CF_EXPECT(VmManager::kMaxDisks >= disk_num,
            "Provided too many disks (" << disk_num << "), maximum "
                                        << VmManager::kMaxDisks << "supported");
  size_t i = 0;
  for (const auto& disk : instance.virtual_disk_paths()) {
    if (instance.vhost_user_block()) {
      auto block = CF_EXPECT(VhostUserBlockDevice(config, i, disk));
      commands.emplace_back(std::move(block.device_cmd));
      commands.emplace_back(std::move(block.device_logs_cmd));
      auto socket_path = std::move(block.socket_path);
      qemu_cmd.AddPrerequisite([socket_path]() -> Result<void> {
#ifdef __linux__
        return WaitForUnixSocketListeningWithoutConnect(socket_path,
                                                        /*timeoutSec=*/30);
#else
        return CF_ERR("Unhandled check if vhost user block ready.");
#endif
      });

      qemu_cmd.AddParameter("-chardev");
      qemu_cmd.AddParameter("socket,id=vhost-user-block-", i,
                            ",path=", socket_path);
      qemu_cmd.AddParameter("-device");
      qemu_cmd.AddParameter(
          "vhost-user-blk-pci-non-transitional,chardev=vhost-user-block-", i);
    } else {
      qemu_cmd.AddParameter("-drive");
      qemu_cmd.AddParameter("file=", disk, ",if=none,id=drive-virtio-disk", i,
                            ",aio=threads");
      qemu_cmd.AddParameter("-device");
      qemu_cmd.AddParameter(
          "virtio-blk-pci-non-transitional,drive=drive-virtio-disk", i,
          ",id=virtio-disk", i, (i == 0 ? ",bootindex=1" : ""));
    }
    ++i;
  }

  if (is_x86 && FileExists(instance.pstore_path())) {
    // QEMU will assign the NVDIMM (ramoops pstore region) 150000000-1501fffff
    // As we will pass this to ramoops, define this region first so it is always
    // located at this address. This is currently x86 only.
    qemu_cmd.AddParameter("-object");
    qemu_cmd.AddParameter("memory-backend-file,id=objpmem0,share=on,mem-path=",
                          instance.pstore_path(), ",size=", pstore_size_bytes);

    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter("nvdimm,memdev=objpmem0,id=ramoops");
  }

  // QEMU does not implement virtio-pmem-pci for ARM64 or RISC-V yet; restore
  // this when the device has been added
  if (is_x86) {
    if (access_kregistry_size_bytes > 0) {
      qemu_cmd.AddParameter("-object");
      qemu_cmd.AddParameter(
          "memory-backend-file,id=objpmem1,share=on,mem-path=",
          instance.access_kregistry_path(),
          ",size=", access_kregistry_size_bytes);

      qemu_cmd.AddParameter("-device");
      qemu_cmd.AddParameter(
          "virtio-pmem-pci,disable-legacy=on,memdev=objpmem1,id=pmem0");
    }
    if (hwcomposer_pmem_size_bytes > 0) {
      qemu_cmd.AddParameter("-object");
      qemu_cmd.AddParameter(
          "memory-backend-file,id=objpmem2,share=on,mem-path=",
          instance.hwcomposer_pmem_path(),
          ",size=", hwcomposer_pmem_size_bytes);

      qemu_cmd.AddParameter("-device");
      qemu_cmd.AddParameter(
          "virtio-pmem-pci,disable-legacy=on,memdev=objpmem2,id=pmem1");
    }
  }

  qemu_cmd.AddParameter("-object");
  qemu_cmd.AddParameter("rng-random,id=objrng0,filename=/dev/urandom");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-rng-pci-non-transitional,rng=objrng0,id=rng0,",
                        "max-bytes=1024,period=2000");

  // TODO: Use the vhost-user devices instead if/when qemu is accessed via
  // webRTC instead of VNC.
  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-mouse-pci,disable-legacy=on");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-keyboard-pci,disable-legacy=on");

  // device padding for unsupported "switches" input
  qemu_cmd.AddParameter("-chardev");
  qemu_cmd.AddParameter("socket,path=", instance.switches_socket_path(), ",id=switches0");
  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("vhost-user-input-pci,chardev=switches0");

  qemu_cmd.AddParameter("-chardev");
  qemu_cmd.AddParameter("socket,path=", instance.rotary_socket_path(), ",id=rotary0");
  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("vhost-user-input-pci,chardev=rotary0");

  auto vhost_net = instance.vhost_net() ? ",vhost=on" : "";

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-balloon-pci-non-transitional,id=balloon0");

  bool has_network_devices = false;
  switch (instance.external_network_mode()) {
    case ExternalNetworkMode::kTap:
      if (instance.enable_tap_devices()) {
        has_network_devices = true;
        qemu_cmd.AddParameter("-netdev");
        qemu_cmd.AddParameter(
            "tap,id=hostnet0,ifname=", instance.mobile_tap_name(),
            ",script=no,downscript=no", vhost_net);

        qemu_cmd.AddParameter("-netdev");
        qemu_cmd.AddParameter(
            "tap,id=hostnet1,ifname=", instance.ethernet_tap_name(),
            ",script=no,downscript=no", vhost_net);

        if (!config.virtio_mac80211_hwsim()) {
          qemu_cmd.AddParameter("-netdev");
          qemu_cmd.AddParameter(
              "tap,id=hostnet2,ifname=", instance.wifi_tap_name(),
              ",script=no,downscript=no", vhost_net);
        }
      }
      break;
    case cuttlefish::ExternalNetworkMode::kSlirp: {
      has_network_devices = true;
      const std::string net =
          fmt::format("{}/{}", instance.ril_ipaddr(), instance.ril_prefixlen());
      const std::string& host = instance.ril_gateway();
      qemu_cmd.AddParameter("-netdev");
      // TODO(schuffelen): `dns` needs to match the first `nameserver` in
      // `/etc/resolv.conf`. Implement something that generalizes beyond
      // gLinux.
      qemu_cmd.AddParameter("user,id=hostnet0,net=", net, ",host=", host,
                            ",dns=127.0.0.1");

      qemu_cmd.AddParameter("-netdev");
      qemu_cmd.AddParameter("user,id=hostnet1,net=10.0.1.1/24,dns=8.8.4.4");

      if (!config.virtio_mac80211_hwsim()) {
        qemu_cmd.AddParameter("-netdev");
        qemu_cmd.AddParameter("user,id=hostnet2,net=10.0.2.1/24,dns=1.1.1.1");
      }
      break;
    }
    default:
      return CF_ERRF("Unexpected net mode {}",
                     instance.external_network_mode());
  }

  if (has_network_devices) {
    // The ordering of virtio-net devices is important. Make sure any change
    // here is reflected in ethprime u-boot variable
    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter(
        "virtio-net-pci-non-transitional,netdev=hostnet0,id=net0,mac=",
        instance.mobile_mac());
    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter(
        "virtio-net-pci-non-transitional,netdev=hostnet1,id=net1,mac=",
        instance.ethernet_mac());
    if (!config.virtio_mac80211_hwsim()) {
      qemu_cmd.AddParameter("-device");
      qemu_cmd.AddParameter(
          "virtio-net-pci-non-transitional,netdev=hostnet2,id=net2,mac=",
          instance.wifi_mac());
    }
  }

  if (is_x86 || is_arm) {
    qemu_cmd.AddParameter("-cpu");
    qemu_cmd.AddParameter(IsHostCompatible(arch_) ? "host" : "max");
  }

  // Explicitly enable the optional extensions of interest, in case the default
  // behavior changes upstream.
  if (is_riscv64) {
    qemu_cmd.AddParameter("-cpu");
    qemu_cmd.AddParameter("rv64",
                          ",v=true,elen=64,vlen=128",
                          ",zba=true,zbb=true,zbs=true");
  }

  qemu_cmd.AddParameter("-msg");
  qemu_cmd.AddParameter("timestamp=on");

#ifdef __linux__
  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("vhost-vsock-pci-non-transitional,guest-cid=",
                        instance.vsock_guest_cid());
#endif

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("AC97,audiodev=audio_none");
  qemu_cmd.AddParameter("-audiodev");
  qemu_cmd.AddParameter("driver=none,id=audio_none");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("qemu-xhci,id=xhci");

  qemu_cmd.AddParameter("-L");
  qemu_cmd.AddParameter(HostQemuBiosPath());

  if (is_riscv64) {
    qemu_cmd.AddParameter("-kernel");
    qemu_cmd.AddParameter(instance.bootloader());
  } else if (is_arm) {
    qemu_cmd.AddParameter("-bios");
    qemu_cmd.AddParameter(instance.bootloader());
  } else {
    qemu_cmd.AddParameter("-drive");
    qemu_cmd.AddParameter("if=pflash,format=raw,readonly=on,file=",
                          instance.bootloader());
    qemu_cmd.AddParameter("-drive");
    qemu_cmd.AddParameter("if=pflash,format=raw,file=", instance.pflash_path());
  }

  if (instance.gdb_port() > 0) {
    qemu_cmd.AddParameter("-S");
    qemu_cmd.AddParameter("-gdb");
    qemu_cmd.AddParameter("tcp::", instance.gdb_port());
  }

  // After all other devices are added, add some more console sinks
  // so it doesn't upset any sepolicy, but works around a QEMU warning
  // when U-Boot probes the ports between kDefaultNumHvcs and
  // kMaxSerialPorts
  while (hvc_num < kMaxSerialPorts) {
    add_hvc_sink();
  }

  commands.emplace_back(std::move(qemu_cmd), true);
  return commands;
}

} // namespace vm_manager
}  // namespace cuttlefish
