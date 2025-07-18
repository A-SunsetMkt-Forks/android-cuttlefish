//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/commands/cvd/instances/instance_database_helper.h"

#include <cstdlib>
#include <string>
#include <optional>
#include <utility>
#include <vector>

#include <android-base/file.h>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/files.h"

namespace cuttlefish {
namespace selector {
namespace {

// mktemp with /tmp/<subdir>.XXXXXX, and if failed,
// mkdir -p /tmp/<subdir>.<default_suffix>
std::optional<std::string> CreateTempDirectory(
    const std::string& subdir, const std::string& default_suffix) {
  std::string path_pattern = "/tmp/" + subdir + ".XXXXXX";
  auto ptr = mkdtemp(path_pattern.data());
  if (ptr) {
    return {std::string(ptr)};
  }
  std::string default_path = "/tmp/" + subdir + "." + default_suffix;
  return (EnsureDirectoryExists(default_path).ok() ? std::optional(default_path)
                                                   : std::nullopt);
}

// Linux "touch" a(n empty) file
bool Touch(const std::string& full_path) {
  // this file is required only to make FileExists() true.
  SharedFD new_file = SharedFD::Creat(full_path, S_IRUSR | S_IWUSR);
  return new_file->IsOpen();
}

}  // namespace

CvdInstanceDatabaseTest::CvdInstanceDatabaseTest()
    : error_{.error_code = ErrorCode::kOk, .msg = ""},
      db_backing_path_("/tmp/cvd_test_db_XXXXXX"),
      db_backing_fd_(mkstemp(db_backing_path_.data())),
      db_(db_backing_path_) {
  if (db_backing_fd_ < 0) {
    SetErrorCode(ErrorCode::kFileError, strerror(errno));
  }
  InitWorkspace() && InitMockAndroidHostOut();
}

CvdInstanceDatabaseTest::~CvdInstanceDatabaseTest() { 
  ClearWorkspace();
  close(db_backing_fd_);
}

void CvdInstanceDatabaseTest::ClearWorkspace() {
  if (!workspace_dir_.empty()) {
    RecursivelyRemoveDirectory(workspace_dir_);
  }
}

void CvdInstanceDatabaseTest::SetErrorCode(const ErrorCode error_code,
                                           const std::string& msg) {
  error_.error_code = error_code;
  error_.msg = msg;
}

bool CvdInstanceDatabaseTest::InitWorkspace() {
  // creating a parent dir of the mock home directories for each fake group
  auto result_opt = CreateTempDirectory("cf_unittest", "default_location");
  if (!result_opt) {
    SetErrorCode(ErrorCode::kFileError, "Failed to create workspace");
    return false;
  }
  workspace_dir_ = std::move(result_opt.value());
  return true;
}

bool CvdInstanceDatabaseTest::InitMockAndroidHostOut() {
  /* creating a fake host out directory
   *
   * As the automated testing system does not guarantee that there is either
   * ANDROID_HOST_OUT or ".", where we can find host tools, we create a fake
   * host tool directory just enough to deceive InstanceDatabase APIs.
   *
   */
  std::string android_host_out = workspace_dir_ + "/android_host_out";
  if (!EnsureDirectoryExists(android_host_out).ok()) {
    SetErrorCode(ErrorCode::kFileError, "Failed to create " + android_host_out);
    return false;
  }
  android_artifacts_path_ = android_host_out;
  if (!EnsureDirectoryExists(android_artifacts_path_ + "/bin").ok()) {
    SetErrorCode(ErrorCode::kFileError,
                 "Failed to create " + android_artifacts_path_ + "/bin");
    return false;
  }
  if (!Touch(android_artifacts_path_ + "/bin" + "/launch_cvd")) {
    SetErrorCode(ErrorCode::kFileError, "Failed to create mock launch_cvd");
    return false;
  }
  return true;
}

  bool CvdInstanceDatabaseTest::AddGroup(const std::string& base_name,
                const std::vector<cvd::Instance>& instances) {
    const std::string home(Workspace() + "/" + base_name);
    if (!EnsureDirectoryExists(home).ok()) {
      SetErrorCode(ErrorCode::kFileError, home + " directory is not found.");
      return false;
    }
    cvd::InstanceGroup param;
    param.set_name(base_name);
    param.set_home_directory(home);
    param.set_host_artifacts_path(android_artifacts_path_);
    param.set_product_out_path(android_artifacts_path_);
    for (const auto& instance: instances) {
      *param.add_instances() = instance;
    }

    if (!db_.AddInstanceGroup(param).ok()) {
      SetErrorCode(ErrorCode::kInstanceDabaseError, "Failed to add group");
      return false;
    }
    return true;
  }

}  // namespace selector
}  // namespace cuttlefish
