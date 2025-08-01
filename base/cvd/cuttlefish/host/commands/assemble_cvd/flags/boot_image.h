/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <stddef.h>

#include <string>
#include <vector>

#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"

namespace cuttlefish {

/* Android kernel path flag, --kernel_path */
class BootImageFlag {
 public:
  static BootImageFlag FromGlobalGflags(const SystemImageDirFlag&);

  std::string BootImageForIndex(size_t index) const;

  bool IsDefault() const;

 private:
  BootImageFlag(const SystemImageDirFlag&, std::vector<std::string>);

  const SystemImageDirFlag& system_image_dir_;
  std::vector<std::string> boot_images_;
};

}  // namespace cuttlefish
