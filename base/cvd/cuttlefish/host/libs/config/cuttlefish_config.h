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
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "cuttlefish/common/libs/utils/architecture.h"
#include "cuttlefish/common/libs/utils/device_type.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/config/ap_boot_flow.h"
#include "cuttlefish/host/libs/config/boot_flow.h"
#include "cuttlefish/host/libs/config/config_constants.h"
#include "cuttlefish/host/libs/config/config_fragment.h"
#include "cuttlefish/host/libs/config/config_utils.h"
#include "cuttlefish/host/libs/config/external_network_mode.h"
#include "cuttlefish/host/libs/config/secure_hals.h"
#include "cuttlefish/host/libs/config/vmm_mode.h"

namespace Json {
class Value;
}

namespace cuttlefish {

enum class GuestHwuiRenderer {
  kUnknown,
  kSkiaGl,
  kSkiaVk,
};
std::ostream& operator<<(std::ostream&, GuestHwuiRenderer);
std::string ToString(GuestHwuiRenderer renderer);
Result<GuestHwuiRenderer> ParseGuestHwuiRenderer(std::string_view);

enum class GuestRendererPreload {
  kAuto,
  kGuestDefault,
  kEnabled,
  kDisabled,
};
std::ostream& operator<<(std::ostream&, GuestRendererPreload);
std::string ToString(GuestRendererPreload);
Result<GuestRendererPreload> ParseGuestRendererPreload(std::string_view);

// Holds the configuration of the cuttlefish instances.
class CuttlefishConfig {
 public:
  static const CuttlefishConfig* Get();
  static std::unique_ptr<const CuttlefishConfig> GetFromFile(
      const std::string& path);
  static bool ConfigExists();

  CuttlefishConfig();
  CuttlefishConfig(CuttlefishConfig&&);
  ~CuttlefishConfig();
  CuttlefishConfig& operator=(CuttlefishConfig&&);

  // Saves the configuration object in a file, it can then be read in other
  // processes by passing the --config_file option.
  bool SaveToFile(const std::string& file) const;
  bool LoadFromFile(const char* file);

  bool SaveFragment(const ConfigFragment&);
  bool LoadFragment(ConfigFragment&) const;

  std::string root_dir() const;
  void set_root_dir(const std::string& root_dir);

  std::string instances_dir() const;
  std::string InstancesPath(const std::string&) const;

  std::string assembly_dir() const;
  std::string AssemblyPath(const std::string&) const;

  void set_instances_uds_dir(const std::string&);
  std::string instances_uds_dir() const;
  std::string InstancesUdsPath(const std::string&) const;

  void set_environments_dir(const std::string&);
  std::string environments_dir() const;
  std::string EnvironmentsPath(const std::string&) const;

  void set_environments_uds_dir(const std::string&);
  std::string environments_uds_dir() const;
  std::string EnvironmentsUdsPath(const std::string&) const;

  VmmMode vm_manager() const;
  void set_vm_manager(VmmMode vmm);

  std::string ap_vm_manager() const;
  void set_ap_vm_manager(const std::string& name);

  struct DisplayConfig {
    int width;
    int height;
    int dpi;
    int refresh_rate_hz;
    std::string overlays;
  };

  struct TouchpadConfig {
    int width;
    int height;

    static Json::Value Serialize(
        const CuttlefishConfig::TouchpadConfig& config);
    static TouchpadConfig Deserialize(const Json::Value& config_json);
  };

  void set_secure_hals(const std::set<SecureHal>&);
  Result<std::set<SecureHal>> secure_hals() const;

  void set_crosvm_binary(const std::string& crosvm_binary);
  std::string crosvm_binary() const;

  void set_gem5_debug_flags(const std::string& gem5_debug_flags);
  std::string gem5_debug_flags() const;

  void set_enable_host_uwb(bool enable_host_uwb);
  bool enable_host_uwb() const;

  void set_enable_host_uwb_connector(bool enable_host_uwb);
  bool enable_host_uwb_connector() const;

  void set_enable_host_bluetooth(bool enable_host_bluetooth);
  bool enable_host_bluetooth() const;

  void set_enable_automotive_proxy(bool enable_automotive_proxy);
  bool enable_automotive_proxy() const;

  // The vsock port used by vhal_proxy_server
  void set_vhal_proxy_server_port(int port);
  int vhal_proxy_server_port() const;

  // Bluetooth is enabled by bt_connector and rootcanal
  void set_enable_host_bluetooth_connector(bool enable_host_bluetooth);
  bool enable_host_bluetooth_connector() const;

  void set_enable_host_nfc(bool enable_host_nfc);
  bool enable_host_nfc() const;

  void set_enable_host_nfc_connector(bool enable_host_nfc_connector);
  bool enable_host_nfc_connector() const;

  void set_casimir_args(const std::string& casimir_args);
  std::vector<std::string> casimir_args() const;
  void set_casimir_instance_num(int casimir_instance_num);
  int casimir_instance_num() const;
  void set_casimir_nci_port(int port);
  int casimir_nci_port() const;
  void set_casimir_rf_port(int port);
  int casimir_rf_port() const;

  // Flags for the set of radios that are connected to netsim
  enum NetsimRadio {
    Bluetooth = 0b00000001,
    Wifi      = 0b00000010,
    Uwb       = 0b00000100,
  };

  void netsim_radio_enable(NetsimRadio flag);
  bool netsim_radio_enabled(NetsimRadio flag) const;
  void set_netsim_instance_num(int netsim_instance_num);
  int netsim_instance_num() const;
  // Netsim has a built-in connector to forward packets to another daemon based
  // on instance number.  This is set in the serial launch case when FLAGS
  // rootcanal_instance_num is specified. The non-netsim case uses
  // bluetooth_connector and rootcanal_hci_port for the same purpose. Purposely
  // restricted to legacy bluetooth serial invocation because new cases should
  // use cvd.
  int netsim_connector_instance_num() const;
  void set_netsim_connector_instance_num(int netsim_instance_num);
  void set_netsim_args(const std::string& netsim_args);
  std::vector<std::string> netsim_args() const;

  enum class Answer {
    kUnknown = 0,
    kYes,
    kNo,
  };

  void set_enable_metrics(std::string enable_metrics);
  CuttlefishConfig::Answer enable_metrics() const;

  void set_metrics_binary(const std::string& metrics_binary);
  std::string metrics_binary() const;

  void set_extra_kernel_cmdline(const std::string& extra_cmdline);
  std::vector<std::string> extra_kernel_cmdline() const;

  // The port for the webrtc signaling server proxy.
  void set_sig_server_proxy_port(int port);
  int sig_server_proxy_port() const;

  // The address of the signaling server
  void set_sig_server_address(const std::string& addr);
  std::string sig_server_address() const;

  // Whether display composition is enabled for one or more displays
  bool OverlaysEnabled() const;

  void set_host_tools_version(const std::map<std::string, uint32_t>&);
  std::map<std::string, uint32_t> host_tools_version() const;

  void set_virtio_mac80211_hwsim(bool virtio_mac80211_hwsim);
  bool virtio_mac80211_hwsim() const;

  void set_ap_rootfs_image(const std::string& ap_rootfs_image);
  std::string ap_rootfs_image() const;

  void set_ap_kernel_image(const std::string& ap_kernel_image);
  std::string ap_kernel_image() const;

  void set_pica_uci_port(int pica_uci_port);
  int pica_uci_port() const;

  void set_rootcanal_args(const std::string& rootcanal_args);
  std::vector<std::string> rootcanal_args() const;

  void set_rootcanal_hci_port(int rootcanal_hci_port);
  int rootcanal_hci_port() const;

  void set_rootcanal_link_port(int rootcanal_link_port);
  int rootcanal_link_port() const;

  void set_rootcanal_link_ble_port(int rootcanal_link_ble_port);
  int rootcanal_link_ble_port() const;

  void set_rootcanal_test_port(int rootcanal_test_port);
  int rootcanal_test_port() const;

  // The path of an AP image in composite disk
  std::string ap_image_dev_path() const;
  void set_ap_image_dev_path(const std::string& dev_path);

  // path to the saved snapshot file(s)
  std::string snapshot_path() const;
  void set_snapshot_path(const std::string& snapshot_path);

  std::set<std::string> straced_host_executables() const;
  void set_straced_host_executables(const std::set<std::string>& executables);

  std::string kvm_path() const;
  void set_kvm_path(const std::string&);

  std::string vhost_vsock_path() const;
  void set_vhost_vsock_path(const std::string&);

  bool IsCrosvm() const;

  class InstanceSpecific;
  class MutableInstanceSpecific;

  MutableInstanceSpecific ForInstance(int instance_num);
  InstanceSpecific ForInstance(int instance_num) const;
  InstanceSpecific ForInstanceName(const std::string& name) const;
  InstanceSpecific ForDefaultInstance() const;

  std::vector<InstanceSpecific> Instances() const;
  std::vector<std::string> instance_dirs() const;

  void set_instance_names(const std::vector<std::string>& instance_names);
  std::vector<std::string> instance_names() const;

  // A view into an existing CuttlefishConfig object for a particular instance.
  class InstanceSpecific {
    const CuttlefishConfig* config_;
    std::string id_;
    friend InstanceSpecific CuttlefishConfig::ForInstance(int num) const;
    friend std::vector<InstanceSpecific> CuttlefishConfig::Instances() const;

    InstanceSpecific(const CuttlefishConfig* config, const std::string& id)
        : config_(config), id_(id) {}

    Json::Value* Dictionary();
    const Json::Value* Dictionary() const;
  public:
    std::string serial_number() const;

    // Index of this instance within current configured group of VMs
    int index() const;

    // If any of the following port numbers is 0, the relevant service is not
    // running on the guest.

    // Port number for qemu to run a vnc server on the host
    int qemu_vnc_server_port() const;
    // Port number to connect to the tombstone receiver on the host
    int tombstone_receiver_port() const;
    // Port number to connect to the config server on the host
    int config_server_port() const;
    // Port number to connect to the audiocontrol server on the guest
    int audiocontrol_server_port() const;
    // Port number to connect to the adb server on the host
    int adb_host_port() const;
    // Port number to connect to the fastboot server on the host
    int fastboot_host_port() const;
    // Device-specific ID to distinguish modem simulators. Must be 4 digits.
    int modem_simulator_host_id() const;
    // Port number to connect to the gnss grpc proxy server on the host
    int gnss_grpc_proxy_server_port() const;
    std::string adb_ip_and_port() const;
    // Port number to connect to the camera hal on the guest
    int camera_server_port() const;
    // Port number to connect to the lights hal on the guest
    int lights_server_port() const;

    std::string adb_device_name() const;
    std::string gnss_file_path() const;
    std::string fixed_location_file_path() const;
    std::string mobile_bridge_name() const;
    std::string mobile_tap_name() const;
    std::string mobile_mac() const;
    std::string wifi_bridge_name() const;
    std::string wifi_tap_name() const;
    std::string wifi_mac() const;
    bool use_bridged_wifi_tap() const;
    std::string ethernet_tap_name() const;
    std::string ethernet_bridge_name() const;
    std::string ethernet_mac() const;
    std::string ethernet_ipv6() const;
    uint32_t session_id() const;
    bool use_allocd() const;
    int vsock_guest_cid() const;
    std::string vsock_guest_group() const;
    std::string uuid() const;
    std::string instance_name() const;
    std::string environment_name() const;
    std::vector<std::string> virtual_disk_paths() const;

    // Returns the path to a file with the given name in the instance
    // directory..
    std::string PerInstancePath(const std::string& file_name) const;
    std::string PerInstanceInternalPath(const std::string& file_name) const;
    std::string PerInstanceLogPath(const std::string& file_name) const;

    std::string CrosvmSocketPath() const;
    std::string OpenwrtCrosvmSocketPath() const;
    std::string instance_dir() const;

    std::string instance_internal_dir() const;

    // Return the Unix domain socket path with given name. Because the
    // length limitation of Unix domain socket name, it needs to be in
    // the another directory than normal Instance path.
    std::string PerInstanceUdsPath(const std::string& file_name) const;
    std::string PerInstanceInternalUdsPath(const std::string& file_name) const;
    std::string PerInstanceGrpcSocketPath(const std::string& socket_name) const;

    std::string instance_uds_dir() const;

    std::string instance_internal_uds_dir() const;

    std::string touch_socket_path(int touch_dev_idx) const;
    std::string mouse_socket_path() const;
    std::string rotary_socket_path() const;
    std::string keyboard_socket_path() const;
    std::string switches_socket_path() const;

    std::string access_kregistry_path() const;

    std::string hwcomposer_pmem_path() const;

    std::string pstore_path() const;

    std::string pflash_path() const;

    std::string console_path() const;

    std::string logcat_path() const;

    std::string kernel_log_pipe_name() const;

    std::string console_pipe_prefix() const;
    std::string console_in_pipe_name() const;
    std::string console_out_pipe_name() const;

    std::string gnss_pipe_prefix() const;
    std::string gnss_in_pipe_name() const;
    std::string gnss_out_pipe_name() const;

    std::string logcat_pipe_name() const;
    std::string restore_adbd_pipe_name() const;

    std::string launcher_log_path() const;

    std::string launcher_monitor_socket_path() const;

    std::string sdcard_path() const;
    std::string sdcard_overlay_path() const;

    std::string persistent_composite_disk_path() const;
    std::string persistent_composite_overlay_path() const;
    std::string persistent_ap_composite_disk_path() const;
    std::string persistent_ap_composite_overlay_path() const;

    std::string os_composite_disk_path() const;

    std::string ap_composite_disk_path() const;

    std::string uboot_env_image_path() const;

    std::string ap_uboot_env_image_path() const;

    std::string ap_esp_image_path() const;

    std::string esp_image_path() const;

    std::string otheros_esp_grub_config() const;

    std::string ap_esp_grub_config() const;

    std::string audio_server_path() const;

    BootFlow boot_flow() const;

    // modem simulator related
    std::string modem_simulator_ports() const;

    // The device id the webrtc process should use to register with the
    // signaling server
    std::string webrtc_device_id() const;

    // Whether this instance should start a rootcanal instance
    bool start_rootcanal() const;

    // Whether this instance should start a casimir instance
    bool start_casimir() const;

    // Whether this instance should start a pica instance
    bool start_pica() const;

    // Whether this instance should start a netsim instance
    bool start_netsim() const;

    // TODO(b/288987294) Remove this when separating environment is done
    // Whether this instance should start a wmediumd instance
    bool start_wmediumd_instance() const;

    const Json::Value& mcu() const;

    APBootFlow ap_boot_flow() const;

    bool crosvm_use_balloon() const;
    bool crosvm_use_rng() const;
    bool crosvm_simple_media_device() const;
    std::string crosvm_v4l2_proxy() const;
    bool use_pmem() const;

    // Wifi MAC address inside the guest
    int wifi_mac_prefix() const;

    std::string id() const;

    std::string gem5_binary_dir() const;

    std::string gem5_checkpoint_dir() const;

    // Serial console
    bool console() const;
    std::string console_dev() const;
    bool enable_sandbox() const;
    bool enable_virtiofs() const;

    // KGDB configuration for kernel debugging
    bool kgdb() const;

    // TODO (b/163575714) add virtio console support to the bootloader so the
    // virtio console path for the console device can be taken again. When that
    // happens, this function can be deleted along with all the code paths it
    // forces.
    bool use_bootloader() const;

    DeviceType device_type() const;

    Arch target_arch() const;

    int cpus() const;

    std::string vcpu_config_path() const;

    std::string data_policy() const;

    int blank_data_image_mb() const;

    int gdb_port() const;

    std::vector<DisplayConfig> display_configs() const;
    std::vector<TouchpadConfig> touchpad_configs() const;

    std::string grpc_socket_path() const;
    int memory_mb() const;
    int ddr_mem_mb() const;
    std::string setupwizard_mode() const;
    std::string userdata_format() const;
    bool guest_enforce_security() const;
    bool use_sdcard() const;
    bool pause_in_bootloader() const;
    bool run_as_daemon() const;
    bool enable_audio() const;
    bool enable_mouse() const;
    std::optional<std::string> custom_keyboard_config() const;
    const Json::Value& domkey_mapping_config() const;
    bool enable_gnss_grpc_proxy() const;
    bool enable_bootanimation() const;
    bool enable_usb() const;
    std::vector<std::string> extra_bootconfig_args() const;
    bool record_screen() const;
    std::string gem5_debug_file() const;
    bool mte() const;
    std::string boot_slot() const;
    bool fail_fast() const;
    bool vhost_user_block() const;
    std::string ti50_emulator() const;
    bool enable_jcard_simulator() const;

    // Kernel and bootloader logging
    bool enable_kernel_log() const;
    bool vhost_net() const;
    bool vhost_user_vsock() const;
    int openthread_node_id() const;

    // Mobile network info (RIL)
    std::string ril_dns() const;
    std::string ril_ipaddr() const;
    std::string ril_gateway() const;
    std::string ril_broadcast() const;
    uint8_t ril_prefixlen() const;

    bool enable_webrtc() const;
    std::string webrtc_assets_dir() const;

    // The range of TCP ports available for webrtc sessions.
    std::pair<uint16_t, uint16_t> webrtc_tcp_port_range() const;

    // The range of UDP ports available for webrtc sessions.
    std::pair<uint16_t, uint16_t> webrtc_udp_port_range() const;

    bool smt() const;
    std::string crosvm_binary() const;
    std::string seccomp_policy_dir() const;
    std::string qemu_binary_dir() const;

    // Configuration flags for a minimal device
    bool enable_minimal_mode() const;
    bool enable_modem_simulator() const;
    int modem_simulator_instance_number() const;
    int modem_simulator_sim_type() const;

    std::string gpu_mode() const;
    std::string gpu_angle_feature_overrides_enabled() const;
    std::string gpu_angle_feature_overrides_disabled() const;
    std::string gpu_capture_binary() const;
    std::string gpu_gfxstream_transport() const;
    std::string gpu_renderer_features() const;
    std::string gpu_context_types() const;
    GuestHwuiRenderer guest_hwui_renderer() const;
    GuestRendererPreload guest_renderer_preload() const;
    std::string guest_vulkan_driver() const;
    bool guest_uses_bgra_framebuffers() const;
    std::string frames_socket_path() const;

    std::string gpu_vhost_user_mode() const;

    bool enable_gpu_udmabuf() const;
    bool enable_gpu_vhost_user() const;
    bool enable_gpu_external_blob() const;
    bool enable_gpu_system_blob() const;

    std::string hwcomposer() const;

    bool restart_subprocesses() const;

    // android artifacts
    std::string boot_image() const;
    std::string new_boot_image() const;
    std::string init_boot_image() const;
    std::string data_image() const;
    std::string new_data_image() const;
    std::string super_image() const;
    std::string new_super_image() const;
    std::string vendor_boot_image() const;
    std::string new_vendor_boot_image() const;
    std::string vbmeta_image() const;
    std::string new_vbmeta_image() const;
    std::string vbmeta_system_image() const;
    std::string vbmeta_vendor_dlkm_image() const;
    std::string new_vbmeta_vendor_dlkm_image() const;
    std::string vbmeta_system_dlkm_image() const;
    std::string new_vbmeta_system_dlkm_image() const;
    std::string vvmtruststore_path() const;
    std::string default_target_zip() const;
    std::string system_target_zip() const;

    // otheros artifacts
    std::string otheros_esp_image() const;

    // android efi loader flow
    std::string android_efi_loader() const;

    // chromeos artifacts for otheros flow
    std::string chromeos_disk() const;
    std::string chromeos_kernel_path() const;
    std::string chromeos_root_image() const;

    // linux artifacts for otheros flow
    std::string linux_kernel_path() const;
    std::string linux_initramfs_path() const;
    std::string linux_root_image() const;

    std::string fuchsia_zedboot_path() const;
    std::string fuchsia_multiboot_bin_path() const;
    std::string fuchsia_root_image() const;

    std::string custom_partition_path() const;

    int blank_sdcard_image_mb() const;
    std::string bootloader() const;
    std::string initramfs_path() const;
    std::string kernel_path() const;
    std::string guest_android_version() const;
    bool bootconfig_supported() const;
    std::string filename_encryption_mode() const;
    ExternalNetworkMode external_network_mode() const;

    bool start_vhal_proxy_server() const;

    int audio_output_streams_count() const;

    bool enable_tap_devices() const;
  };

  // A view into an existing CuttlefishConfig object for a particular instance.
  class MutableInstanceSpecific {
    CuttlefishConfig* config_;
    std::string id_;
    friend MutableInstanceSpecific CuttlefishConfig::ForInstance(int num);

    MutableInstanceSpecific(CuttlefishConfig* config, const std::string& id);

    Json::Value* Dictionary();
  public:
    void set_serial_number(const std::string& serial_number);
    void set_qemu_vnc_server_port(int qemu_vnc_server_port);
    void set_tombstone_receiver_port(int tombstone_receiver_port);
    void set_config_server_port(int config_server_port);
    void set_frames_server_port(int config_server_port);
    void set_touch_server_port(int config_server_port);
    void set_keyboard_server_port(int config_server_port);
    void set_gatekeeper_vsock_port(int gatekeeper_vsock_port);
    void set_keymaster_vsock_port(int keymaster_vsock_port);
    void set_audiocontrol_server_port(int audiocontrol_server_port);
    void set_lights_server_port(int lights_server_port);
    void set_adb_host_port(int adb_host_port);
    void set_modem_simulator_host_id(int modem_simulator_id);
    void set_adb_ip_and_port(const std::string& ip_port);
    void set_fastboot_host_port(int fastboot_host_port);
    void set_camera_server_port(int camera_server_port);
    void set_mobile_bridge_name(const std::string& mobile_bridge_name);
    void set_mobile_tap_name(const std::string& mobile_tap_name);
    void set_mobile_mac(const std::string& mac);
    void set_wifi_bridge_name(const std::string& wifi_bridge_name);
    void set_wifi_tap_name(const std::string& wifi_tap_name);
    void set_wifi_mac(const std::string& mac);
    void set_use_bridged_wifi_tap(bool use_bridged_wifi_tap);
    void set_ethernet_tap_name(const std::string& ethernet_tap_name);
    void set_ethernet_bridge_name(const std::string& set_ethernet_bridge_name);
    void set_ethernet_mac(const std::string& mac);
    void set_ethernet_ipv6(const std::string& ip);
    void set_session_id(uint32_t session_id);
    void set_use_allocd(bool use_allocd);
    void set_vsock_guest_cid(int vsock_guest_cid);
    void set_vsock_guest_group(const std::string& vsock_guest_group);
    void set_uuid(const std::string& uuid);
    void set_environment_name(const std::string& env_name);
    // modem simulator related
    void set_modem_simulator_ports(const std::string& modem_simulator_ports);
    void set_virtual_disk_paths(const std::vector<std::string>& disk_paths);
    void set_webrtc_device_id(const std::string& id);
    void set_start_rootcanal(bool start);
    void set_start_casimir(bool start);
    void set_start_pica(bool start);
    void set_start_netsim(bool start);
    // TODO(b/288987294) Remove this when separating environment is done
    void set_start_wmediumd_instance(bool start);
    void set_mcu(const Json::Value &cfg);
    void set_ap_boot_flow(APBootFlow flow);
    void set_crosvm_use_balloon(const bool use_balloon);
    void set_crosvm_use_rng(const bool use_rng);
    void set_crosvm_simple_media_device(const bool simple_media_device);
    void set_crosvm_v4l2_proxy(const std::string v4l2_proxy);
    void set_use_pmem(const bool use_pmem);
    // Wifi MAC address inside the guest
    void set_wifi_mac_prefix(const int wifi_mac_prefix);
    // Gnss grpc proxy server port inside the host
    void set_gnss_grpc_proxy_server_port(int gnss_grpc_proxy_server_port);
    // Gnss grpc proxy local file path
    void set_gnss_file_path(const std::string &gnss_file_path);
    void set_fixed_location_file_path(
        const std::string& fixed_location_file_path);
    void set_gem5_binary_dir(const std::string& gem5_binary_dir);
    void set_gem5_checkpoint_dir(const std::string& gem5_checkpoint_dir);
    // Serial console
    void set_console(bool console);
    void set_enable_sandbox(const bool enable_sandbox);
    void set_enable_virtiofs(const bool enable_virtiofs);
    void set_kgdb(bool kgdb);
    void set_device_type(DeviceType type);
    void set_target_arch(Arch target_arch);
    void set_cpus(int cpus);
    void set_vcpu_config_path(const std::string& vcpu_config_path);
    void set_data_policy(const std::string& data_policy);
    void set_blank_data_image_mb(int blank_data_image_mb);
    void set_gdb_port(int gdb_port);
    void set_display_configs(const std::vector<DisplayConfig>& display_configs);
    void set_touchpad_configs(
        const std::vector<TouchpadConfig>& touchpad_configs);
    void set_memory_mb(int memory_mb);
    void set_ddr_mem_mb(int ddr_mem_mb);
    Result<void> set_setupwizard_mode(const std::string& mode);
    void set_userdata_format(const std::string& userdata_format);
    void set_guest_enforce_security(bool guest_enforce_security);
    void set_use_sdcard(bool use_sdcard);
    void set_pause_in_bootloader(bool pause_in_bootloader);
    void set_run_as_daemon(bool run_as_daemon);
    void set_enable_audio(bool enable);
    void set_enable_mouse(bool enable);
    void set_custom_keyboard_config(
        const std::string& custom_keyboard_config_json_path);
    void set_domkey_mapping_config(
        const std::string& domkey_mapping_config_json_path);
    void set_enable_usb(bool enable);
    void set_enable_gnss_grpc_proxy(const bool enable_gnss_grpc_proxy);
    void set_enable_bootanimation(const bool enable_bootanimation);
    void set_extra_bootconfig_args(const std::string& extra_bootconfig_args);
    void set_record_screen(bool record_screen);
    void set_gem5_debug_file(const std::string& gem5_debug_file);
    void set_mte(bool mte);
    void set_boot_slot(const std::string& boot_slot);
    void set_grpc_socket_path(const std::string& socket_path);
    void set_fail_fast(bool fail_fast);
    void set_vhost_user_block(bool qemu_vhost_user_block);
    void set_ti50_emulator(const std::string& ti50_emulator);
    // jcardsimulator
    void set_enable_jcard_simulator(bool enable);

    // Kernel and bootloader logging
    void set_enable_kernel_log(bool enable_kernel_log);

    void set_enable_webrtc(bool enable_webrtc);
    void set_webrtc_assets_dir(const std::string& webrtc_assets_dir);

    // The range of TCP ports available for webrtc sessions.
    void set_webrtc_tcp_port_range(std::pair<uint16_t, uint16_t> range);

    // The range of UDP ports available for webrtc sessions.
    void set_webrtc_udp_port_range(std::pair<uint16_t, uint16_t> range);

    void set_smt(bool smt);
    void set_crosvm_binary(const std::string& crosvm_binary);
    void set_seccomp_policy_dir(const std::string& seccomp_policy_dir);
    void set_qemu_binary_dir(const std::string& qemu_binary_dir);

    void set_vhost_net(bool vhost_net);
    void set_vhost_user_vsock(bool vhost_user_vsock);

    void set_openthread_node_id(int node_id);

    // Mobile network (RIL)
    void set_ril_dns(const std::string& ril_dns);
    void set_ril_ipaddr(const std::string& ril_ipaddr);
    void set_ril_gateway(const std::string& ril_gateway);
    void set_ril_broadcast(const std::string& ril_broadcast);
    void set_ril_prefixlen(uint8_t ril_prefixlen);

    // Configuration flags for a minimal device
    void set_enable_minimal_mode(bool enable_minimal_mode);
    void set_enable_modem_simulator(bool enable_modem_simulator);
    void set_modem_simulator_instance_number(int instance_numbers);
    void set_modem_simulator_sim_type(int sim_type);

    void set_gpu_mode(const std::string& name);
    void set_gpu_angle_feature_overrides_enabled(const std::string& overrides);
    void set_gpu_angle_feature_overrides_disabled(const std::string& overrides);
    void set_gpu_capture_binary(const std::string&);
    void set_gpu_gfxstream_transport(const std::string& transport);
    void set_gpu_renderer_features(const std::string& features);
    void set_gpu_context_types(const std::string& context_types);
    void set_guest_hwui_renderer(GuestHwuiRenderer renderer);
    void set_guest_renderer_preload(GuestRendererPreload preload);
    void set_guest_vulkan_driver(const std::string& driver);
    void set_guest_uses_bgra_framebuffers(bool uses_bgra);
    void set_frames_socket_path(const std::string& frame_socket_path);

    void set_enable_gpu_udmabuf(const bool enable_gpu_udmabuf);
    void set_enable_gpu_vhost_user(const bool enable_gpu_vhost_user);
    void set_enable_gpu_external_blob(const bool enable_gpu_external_blob);
    void set_enable_gpu_system_blob(const bool enable_gpu_system_blob);

    void set_hwcomposer(const std::string&);

    void set_restart_subprocesses(bool restart_subprocesses);

    // system image files
    void set_boot_image(const std::string& boot_image);
    void set_new_boot_image(const std::string& new_boot_image);
    void set_init_boot_image(const std::string& init_boot_image);
    void set_data_image(const std::string& data_image);
    void set_new_data_image(const std::string& new_data_image);
    void set_super_image(const std::string& super_image);
    void set_new_super_image(const std::string& super_image);
    void set_vendor_boot_image(const std::string& vendor_boot_image);
    void set_new_vendor_boot_image(const std::string& new_vendor_boot_image);
    void set_vbmeta_image(const std::string& vbmeta_image);
    void set_new_vbmeta_image(const std::string& new_vbmeta_image);
    void set_vbmeta_system_image(const std::string& vbmeta_system_image);
    void set_vbmeta_vendor_dlkm_image(
        const std::string& vbmeta_vendor_dlkm_image);
    void set_new_vbmeta_vendor_dlkm_image(
        const std::string& vbmeta_vendor_dlkm_image);
    void set_vbmeta_system_dlkm_image(
        const std::string& vbmeta_system_dlkm_image);
    void set_new_vbmeta_system_dlkm_image(
        const std::string& vbmeta_system_dlkm_image);
    void set_vvmtruststore_path(const std::string& vvmtruststore_path);
    void set_default_target_zip(const std::string& default_target_zip);
    void set_system_target_zip(const std::string& system_target_zip);
    void set_otheros_esp_image(const std::string& otheros_esp_image);
    void set_android_efi_loader(const std::string& android_efi_loader);
    void set_chromeos_disk(const std::string& chromeos_disk);
    void set_chromeos_kernel_path(const std::string& chromeos_kernel_path);
    void set_chromeos_root_image(const std::string& chromeos_root_image);
    void set_linux_kernel_path(const std::string& linux_kernel_path);
    void set_linux_initramfs_path(const std::string& linux_initramfs_path);
    void set_linux_root_image(const std::string& linux_root_image);
    void set_fuchsia_zedboot_path(const std::string& fuchsia_zedboot_path);
    void set_fuchsia_multiboot_bin_path(const std::string& fuchsia_multiboot_bin_path);
    void set_fuchsia_root_image(const std::string& fuchsia_root_image);
    void set_custom_partition_path(const std::string& custom_partition_path);
    void set_blank_sdcard_image_mb(int blank_sdcard_image_mb);
    void set_bootloader(const std::string& bootloader);
    void set_initramfs_path(const std::string& initramfs_path);
    void set_kernel_path(const std::string& kernel_path);
    void set_guest_android_version(const std::string& guest_android_version);
    void set_bootconfig_supported(bool bootconfig_supported);
    void set_filename_encryption_mode(const std::string& filename_encryption_mode);
    void set_external_network_mode(ExternalNetworkMode network_mode);

    // Whether we should start vhal_proxy_server for the guest-side VHAL to
    // connect to.
    void set_start_vhal_proxy_server(bool start_vhal_proxy_server);

    void set_audio_output_streams_count(int count);

    void set_enable_tap_devices(bool);

   private:
    void SetPath(const std::string& key, const std::string& path);
  };

  class EnvironmentSpecific;
  class MutableEnvironmentSpecific;

  MutableEnvironmentSpecific ForEnvironment(const std::string& envName);
  EnvironmentSpecific ForEnvironment(const std::string& envName) const;

  MutableEnvironmentSpecific ForDefaultEnvironment();
  EnvironmentSpecific ForDefaultEnvironment() const;

  std::vector<std::string> environment_dirs() const;

  class EnvironmentSpecific {
    friend EnvironmentSpecific CuttlefishConfig::ForEnvironment(
        const std::string&) const;
    friend EnvironmentSpecific CuttlefishConfig::ForDefaultEnvironment() const;

    const CuttlefishConfig* config_;
    std::string envName_;

    EnvironmentSpecific(const CuttlefishConfig* config,
                        const std::string& envName)
        : config_(config), envName_(envName) {}

    Json::Value* Dictionary();
    const Json::Value* Dictionary() const;

   public:
    std::string environment_name() const;

    std::string environment_uds_dir() const;
    std::string PerEnvironmentUdsPath(const std::string& file_name) const;

    std::string environment_dir() const;
    std::string PerEnvironmentPath(const std::string& file_name) const;

    std::string PerEnvironmentLogPath(const std::string& file_name) const;

    std::string PerEnvironmentGrpcSocketPath(
        const std::string& file_name) const;

    std::string control_socket_path() const;
    std::string launcher_log_path() const;

    std::string casimir_nci_socket_path() const;
    std::string casimir_rf_socket_path() const;

    // wmediumd related configs
    bool enable_wifi() const;
    bool start_wmediumd() const;
    std::string vhost_user_mac80211_hwsim() const;
    std::string wmediumd_api_server_socket() const;
    std::string wmediumd_config() const;
    int wmediumd_mac_prefix() const;
    int group_uuid() const;
  };

  class MutableEnvironmentSpecific {
    friend MutableEnvironmentSpecific CuttlefishConfig::ForEnvironment(
        const std::string&);
    friend MutableEnvironmentSpecific CuttlefishConfig::ForDefaultEnvironment();

    CuttlefishConfig* config_;
    std::string envName_;

    MutableEnvironmentSpecific(CuttlefishConfig* config,
                               const std::string& envName)
        : config_(config), envName_(envName) {}

    Json::Value* Dictionary();

   public:
    // wmediumd related configs
    void set_enable_wifi(const bool enable_wifi);
    void set_start_wmediumd(bool start);
    void set_vhost_user_mac80211_hwsim(const std::string& path);
    void set_wmediumd_api_server_socket(const std::string& path);
    void set_wmediumd_config(const std::string& config_path);
    void set_wmediumd_mac_prefix(int mac_prefix);

    void set_group_uuid(const int group_uuid);
  };

 private:
  std::unique_ptr<Json::Value> dictionary_;

  static CuttlefishConfig* BuildConfigImpl(const std::string& path);

  CuttlefishConfig(const CuttlefishConfig&) = delete;
  CuttlefishConfig& operator=(const CuttlefishConfig&) = delete;
};

// Whether the instance is restored from a snapshot. Stays true until the device
// reboots.
// When the device is booting, the config  init function checks if
// "FLAGS_snapshot_path" is not empty, and if it isn't empty, a file called
// "restore" will be created to keep track of the restore.
// This is necessary because we don't want to
// modify the config when the device boots, however we also want to only restore
// once. Tracking via "restore" is necessary as a bug existed when checking if
// "snapshot_path" existed during boot, where a restart or a powerwash of the
// device would actually perform a restore instead of their respective actions.
bool IsRestoring(const CuttlefishConfig&);

}  // namespace cuttlefish
