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

#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {

Result<std::vector<std::string>> ExtractImages(
    const std::string& archive_filepath, const std::string& target_directory,
    const std::vector<std::string>& images, bool keep_archive);

Result<std::string> ExtractImage(const std::string& archive_filepath,
                                 const std::string& target_directory,
                                 const std::string& image,
                                 bool keep_archive = true);

Result<std::vector<std::string>> ExtractArchiveContents(
    const std::string& archive_filepath, const std::string& target_directory,
    bool keep_archive);

std::string ExtractArchiveToMemory(const std::string& archive_filepath,
                                   const std::string& archive_member);

std::vector<std::string> ArchiveContents(const std::string& archive);

} // namespace cuttlefish
