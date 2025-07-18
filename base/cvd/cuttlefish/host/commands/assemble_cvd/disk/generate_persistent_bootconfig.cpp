/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/disk/generate_persistent_bootconfig.h"

#include <memory>
#include <optional>
#include <string>

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/size_utils.h"
#include "cuttlefish/host/commands/assemble_cvd/bootconfig_args.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/generate_persistent_bootconfig.h"
#include "cuttlefish/host/libs/avb/avb.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/data_image.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"

namespace cuttlefish {

Result<std::optional<BootConfigPartition>> BootConfigPartition::CreateIfNeeded(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  //  Cuttlefish for the time being won't be able to support OTA from a
  //  non-bootconfig kernel to a bootconfig-kernel (or vice versa) IF the
  //  device is stopped (via stop_cvd). This is rarely an issue since OTA
  //  testing run on cuttlefish is done within one launch cycle of the device.
  //  If this ever becomes an issue, this code will have to be rewritten.
  if (!instance.bootconfig_supported()) {
    return std::nullopt;
  }
  const std::string bootconfig_path =
      instance.PerInstanceInternalPath("bootconfig");

  if (!FileExists(bootconfig_path)) {
    CF_EXPECT(CreateBlankImage(bootconfig_path, 1 /* mb */, "none"),
              "Failed to create image at " << bootconfig_path);
  }

  auto bootconfig_fd = SharedFD::Open(bootconfig_path, O_RDWR);
  CF_EXPECT(bootconfig_fd->IsOpen(),
            "Unable to open bootconfig file: " << bootconfig_fd->StrError());

  const auto bootconfig_args =
      CF_EXPECT(BootconfigArgsFromConfig(config, instance));
  const auto bootconfig =
      CF_EXPECT(BootconfigArgsString(bootconfig_args, "\n")) + "\n";

  LOG(DEBUG) << "bootconfig size is " << bootconfig.size();
  ssize_t bytesWritten = WriteAll(bootconfig_fd, bootconfig);
  CF_EXPECT(WriteAll(bootconfig_fd, bootconfig) == bootconfig.size(),
            "Failed to write bootconfig to \"" << bootconfig_path << "\"");
  LOG(DEBUG) << "Bootconfig parameters from vendor boot image and config are "
             << ReadFile(bootconfig_path);

  CF_EXPECT(bootconfig_fd->Truncate(bootconfig.size()) == 0,
            "`truncate --size=" << bootconfig.size() << " bytes "
                                << bootconfig_path
                                << "` failed:" << bootconfig_fd->StrError());

  if (config.vm_manager() == VmmMode::kGem5) {
    const off_t bootconfig_size_bytes_gem5 =
        AlignToPowerOf2(bytesWritten, PARTITION_SIZE_SHIFT);
    CF_EXPECT(bootconfig_fd->Truncate(bootconfig_size_bytes_gem5) == 0);
    bootconfig_fd->Close();
  } else {
    bootconfig_fd->Close();
    const off_t bootconfig_size_bytes = AlignToPowerOf2(
        kMaxAvbMetadataSize + bootconfig.size(), PARTITION_SIZE_SHIFT);

    std::unique_ptr<Avb> avbtool = GetDefaultAvb();
    CF_EXPECT(avbtool->AddHashFooter(bootconfig_path, "bootconfig",
                                     bootconfig_size_bytes));
  }

  return BootConfigPartition(std::move(bootconfig_path));
}

BootConfigPartition::BootConfigPartition(std::string path) : path_(path) {}

const std::string& BootConfigPartition::Path() const { return path_; }

ImagePartition BootConfigPartition::Partition() const {
  return ImagePartition{
      .label = "bootconfig",
      .image_file_path = AbsolutePath(path_),
  };
}

}  // namespace cuttlefish
