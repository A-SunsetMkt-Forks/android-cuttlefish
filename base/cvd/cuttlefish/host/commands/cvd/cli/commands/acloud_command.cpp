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

#include "cuttlefish/host/commands/cvd/cli/commands/acloud_command.h"

#include <signal.h>  // IWYU pragma: keep
#include <stdlib.h>

#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/cvd/acloud/converter.h"
#include "cuttlefish/host/commands/cvd/acloud/create_converter_parser.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/command_sequence.h"
#include "cuttlefish/host/commands/cvd/cli/commands/acloud_common.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    R"(Toggles translation of acloud commands to run through cvd if supported)";

constexpr char kDetailedHelpText[] = R"(
Usage:
cvd acloud translator (--opt-out|--opt-in)
Any acloud command will by default (and if supported by cvd) be translated to the appropriate cvd command and executed.
If not supported by cvd, acloud will be used.

To opt out or opt back in, run this translation toggle.
)";

class AcloudCommand : public CvdCommandHandler {
 public:
  AcloudCommand(CommandSequenceExecutor& executor) : executor_(executor) {}
  ~AcloudCommand() = default;

  Result<bool> CanHandle(const CommandRequest& request) const override {
    std::vector<std::string> subcmd_args = request.SubcommandArguments();
    if (subcmd_args.size() >= 2) {
      if (request.Subcommand() == "acloud" &&
          (subcmd_args[0] == "translator" ||
           subcmd_args[0] == "mix-super-image")) {
        return false;
      }
    }
    return request.Subcommand() == "acloud";
  }

  cvd_common::Args CmdList() const override { return {"acloud"}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

  /**
   * The `acloud` command satisfies the original `acloud CLI` command using
   * either:
   *
   * 1. `cvd` for local instance management
   *
   * 2. Or `cvdr` for remote instance management.
   *
   */
  Result<void> Handle(const CommandRequest& request) override {
    auto result = ValidateLocal(request);
    if (result.ok()) {
      CF_EXPECT(HandleLocal(*result, request));
      return {};
    } else if (ValidateRemoteArgs(request)) {
      CF_EXPECT(HandleRemote(request));
      return {};
    }
    CF_EXPECT(std::move(result));
    return {};
  }

 private:
  Result<ConvertedAcloudCreateCommand> ValidateLocal(
      const CommandRequest& request);
  bool ValidateRemoteArgs(const CommandRequest& request);
  Result<void> HandleLocal(const ConvertedAcloudCreateCommand& command,
                           const CommandRequest& request);
  Result<void> HandleRemote(const CommandRequest& request);
  Result<void> RunAcloudConnect(const CommandRequest& request,
                                const std::string& hostname);

  CommandSequenceExecutor& executor_;
};

Result<ConvertedAcloudCreateCommand> AcloudCommand::ValidateLocal(
    const CommandRequest& request) {
  CF_EXPECT(CanHandle(request));
  CF_EXPECT(IsSubOperationSupported(request));
  // ConvertAcloudCreate converts acloud to cvd commands.
  return acloud_impl::ConvertAcloudCreate(request);
}

bool AcloudCommand::ValidateRemoteArgs(const CommandRequest& request) {
  std::vector<std::string> args = request.SubcommandArguments();
  return acloud_impl::CompileFromAcloudToCvdr(args).ok();
}

Result<void> AcloudCommand::HandleLocal(
    const ConvertedAcloudCreateCommand& command,
    const CommandRequest& request) {
  CF_EXPECT(executor_.Execute(command.prep_requests, std::cerr));
  CF_EXPECT(executor_.ExecuteOne(command.start_request, std::cerr));

  if (!command.fetch_command_str.empty()) {
    // has cvd fetch command, update the fetch cvd command file
    using android::base::WriteStringToFile;
    CF_EXPECT(WriteStringToFile(command.fetch_command_str,
                                command.fetch_cvd_args_file));
  }
  return {};
}

Result<void> AcloudCommand::HandleRemote(const CommandRequest& request) {
  std::vector<std::string> args = request.SubcommandArguments();
  args = CF_EXPECT(acloud_impl::CompileFromAcloudToCvdr(args));
  Command cmd = Command("cvdr");
  for (auto a : args) {
    cmd.AddParameter(a);
  }
  // Do not perform ADB connection with `cvdr` until acloud CLI is fully
  // deprecated.
  if (args[0] == "create") {
    cmd.AddParameter("--auto_connect=false");
  }

  std::string stdout_;
  SharedFD stdout_pipe_read, stdout_pipe_write;
  CF_EXPECT(SharedFD::Pipe(&stdout_pipe_read, &stdout_pipe_write),
            "Could not create a pipe");
  cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, stdout_pipe_write);
  std::thread stdout_thread([stdout_pipe_read, &stdout_]() {
    int read = ReadAll(stdout_pipe_read, &stdout_);
    if (read < 0) {
      LOG(ERROR) << "Error in reading stdout from process";
    }
  });
  std::cerr
      << "UPDATE! Try the new `cvdr` tool directly. Run `cvdr --help` to get "
         "started.\n";

  siginfo_t siginfo;  // NOLINT(misc-include-cleaner)

  cmd.Start().Wait(&siginfo, WEXITED);
  {
    // Force the destructor to run by moving it into a smaller scope.
    // This is necessary to close the write end of the pipe.
    Command forceDelete = std::move(cmd);
  }
  stdout_pipe_write->Close();
  stdout_thread.join();
  std::cout << stdout_;
  // NOLINTNEXTLINE(misc-include-cleaner)
  if (args[0] == "create" && siginfo.si_status == EXIT_SUCCESS) {
    std::string hostname = stdout_.substr(0, stdout_.find(" "));
    CF_EXPECT(RunAcloudConnect(request, hostname));
  }
  return {};
}

Result<void> AcloudCommand::RunAcloudConnect(const CommandRequest& request,
                                             const std::string& hostname) {
  auto build_top = StringFromEnv("ANDROID_BUILD_TOP", "");
  CF_EXPECT(
      !build_top.empty(),
      "Missing ANDROID_BUILD_TOP environment variable. Please run `source "
      "build/envsetup.sh`");
  Command cmd =
      Command(build_top + "/prebuilts/asuite/acloud/linux-x86/acloud");
  cmd.AddParameter("reconnect");
  cmd.AddParameter("--instance-names");
  cmd.AddParameter(hostname);

  cmd.Start().Wait();

  return {};
}

}  // namespace

std::unique_ptr<CvdCommandHandler> NewAcloudCommand(
    CommandSequenceExecutor& executor) {
  return std::unique_ptr<CvdCommandHandler>(new AcloudCommand(executor));
}

}  // namespace cuttlefish
