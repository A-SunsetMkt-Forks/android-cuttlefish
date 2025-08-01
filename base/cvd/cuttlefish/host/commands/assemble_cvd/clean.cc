/*
 * Copyright (C) 2020 The Android Open Source Project
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
#include "cuttlefish/host/commands/assemble_cvd/clean.h"

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

#include "cuttlefish/common/libs/utils/in_sandbox.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/host/libs/config/config_utils.h"

namespace cuttlefish {
namespace {

Result<void> CleanPriorFiles(const std::string& path,
                             const std::set<std::string>& preserving) {
  if (preserving.count(android::base::Basename(path))) {
    LOG(DEBUG) << "Preserving: " << path;
    return {};
  }
  struct stat statbuf;
  if (lstat(path.c_str(), &statbuf) < 0) {
    int error_num = errno;
    if (error_num == ENOENT) {
      return {};
    } else {
      return CF_ERRNO("Could not stat \"" << path);
    }
  }
  if ((statbuf.st_mode & S_IFMT) != S_IFDIR) {
    LOG(DEBUG) << "Deleting: " << path;
    if (unlink(path.c_str()) < 0) {
      return CF_ERRNO("Could not unlink \"" << path << "\"");
    }
    return {};
  }
  std::unique_ptr<DIR, int(*)(DIR*)> dir(opendir(path.c_str()), closedir);
  if (!dir) {
    return CF_ERRNO("Could not clean \"" << path << "\"");
  }
  for (auto entity = readdir(dir.get()); entity != nullptr; entity = readdir(dir.get())) {
    std::string entity_name(entity->d_name);
    if (entity_name == "." || entity_name == "..") {
      continue;
    }
    std::string entity_path = path + "/" + entity_name;
    CF_EXPECT(CleanPriorFiles(entity_path.c_str(), preserving),
              "CleanPriorFiles for \""
                  << path << "\" failed on recursing into \"" << entity_path
                  << "\"");
  }
  if (rmdir(path.c_str()) < 0) {
    if (!(errno == EEXIST || errno == ENOTEMPTY || errno == EROFS ||
          errno == EBUSY)) {
      // If EEXIST or ENOTEMPTY, probably because a file was preserved. EROFS
      // or EBUSY likely means a bind mount for host-sandboxing mode.
      return CF_ERRF("Could not rmdir '{}': '{}'", path, strerror(errno));
    }
  }
  return {};
}

Result<void> CleanPriorFiles(const std::vector<std::string>& paths,
                             const std::set<std::string>& preserving) {
  std::vector<std::string> prior_dirs;
  std::vector<std::string> prior_files;
  for (const auto& path : paths) {
    struct stat statbuf;
    if (stat(path.c_str(), &statbuf) < 0) {
      if (errno == ENOENT) {
        continue;  // it doesn't exist yet, so there is no work to do
      }
      return CF_ERRNO("Could not stat \"" << path << "\"");
    }
    bool is_directory = (statbuf.st_mode & S_IFMT) == S_IFDIR;
    (is_directory ? prior_dirs : prior_files).emplace_back(path);
  }
  LOG(DEBUG) << fmt::format("Prior dirs: {}", fmt::join(prior_dirs, ", "));
  LOG(DEBUG) << fmt::format("Prior files: {}", fmt::join(prior_files, ", "));

  // TODO(schuffelen): Fix logic for host-sandboxing mode.
  if (!InSandbox() && (!prior_dirs.empty() || !prior_files.empty())) {
    Command lsof("lsof");
    lsof.AddParameter("-t");
    lsof.AddParameter("-Q");  // ignore failed search terms
    for (const auto& prior_dir : prior_dirs) {
      lsof.AddParameter("+D").AddParameter(prior_dir);
    }
    for (const auto& prior_file : prior_files) {
      lsof.AddParameter(prior_file);
    }

    Result<std::string> lsof_out = RunAndCaptureStdout(std::move(lsof));
    if (lsof_out.ok()) {
      std::vector<std::string> pids = android::base::Split(*lsof_out, "\n");
      CF_EXPECTF(
          lsof_out->empty(),
          "Instance directory files in use. Try `cvd reset`? Observed PIDs: {}",
          fmt::join(pids, ", "));
    } else {
      LOG(ERROR) << "Failed to run `lsof`: " << lsof_out.error().FormatForEnv();
    }
  }

  for (const auto& path : paths) {
    CF_EXPECT(CleanPriorFiles(path, preserving),
              "CleanPriorFiles failed for \"" << path << "\"");
  }
  return {};
}

} // namespace

Result<void> CleanPriorFiles(const std::set<std::string>& preserving,
                             const std::vector<std::string>& clean_dirs) {
  std::vector<std::string> paths = {
      // The global link to the config file
      GetGlobalConfigFileLink(),
  };
  paths.insert(paths.end(), clean_dirs.begin(), clean_dirs.end());
  using android::base::Join;
  CF_EXPECT(CleanPriorFiles(paths, preserving),
            "CleanPriorFiles("
                << "paths = {" << Join(paths, ", ") << "}, "
                << "preserving = {" << Join(preserving, ", ") << "}) failed");
  return {};
}

} // namespace cuttlefish
