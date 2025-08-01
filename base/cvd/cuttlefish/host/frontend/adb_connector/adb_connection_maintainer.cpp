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

#include "cuttlefish/host/frontend/adb_connector/adb_connection_maintainer.h"

#include <unistd.h>

#include <cctype>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"

namespace cuttlefish {
namespace {

std::string MakeMessage(const std::string& user_message) {
  std::ostringstream ss;
  ss << std::setfill('0') << std::setw(4) << std::hex << user_message.size()
     << user_message;
  return ss.str();
}

std::string MakeShellUptimeMessage() {
  return MakeMessage("shell,raw:cut -d. -f1 /proc/uptime");
}

std::string MakeShellTradeInModeGetStatusMessage() {
  return MakeMessage("shell,raw:tradeinmode getstatus");
}

std::string MakeTransportMessage(const std::string& address) {
  return MakeMessage("host:transport:" + address);
}

std::string MakeConnectMessage(const std::string& address) {
  return MakeMessage("host:connect:" + address);
}

std::string MakeDisconnectMessage(const std::string& address) {
  return MakeMessage("host:disconnect:" + address);
}

std::string MakeGetStateMessage(const std::string& address) {
  return MakeMessage("host-serial:" + address + ":get-state");
}

// Response will either be OKAY or FAIL
constexpr char kAdbOkayStatusResponse[] = "OKAY";
constexpr std::size_t kAdbStatusResponseLength =
    sizeof kAdbOkayStatusResponse - 1;
constexpr std::string_view kAdbUnauthorizedMsg = "device unauthorized.";
// adb sends the length of what is to follow as a 4 characters string of hex
// digits
constexpr std::size_t kAdbMessageLengthLength = 4;

constexpr int kAdbDaemonPort = 5037;

bool AdbSendMessage(const SharedFD& sock, const std::string& message) {
  if (!sock->IsOpen()) {
    return false;
  }
  if (!SendAll(sock, message)) {
    LOG(WARNING) << "failed to send all bytes to adb daemon";
    return false;
  }

  return RecvAll(sock, kAdbStatusResponseLength) == kAdbOkayStatusResponse;
}

bool AdbSendMessage(const std::string& message) {
  auto sock = SharedFD::SocketLocalClient(kAdbDaemonPort, SOCK_STREAM);
  return AdbSendMessage(sock, message);
}

bool AdbConnect(const std::string& address) {
  return AdbSendMessage(MakeConnectMessage(address));
}

bool AdbDisconnect(const std::string& address) {
  return AdbSendMessage(MakeDisconnectMessage(address));
}

bool IsHexInteger(const std::string& str) {
  return !str.empty() && std::all_of(str.begin(), str.end(),
                                     [](char c) { return std::isxdigit(c); });
}

bool IsInteger(const std::string& str) {
  return !str.empty() && std::all_of(str.begin(), str.end(),
                                     [](char c) { return std::isdigit(c); });
}

// assumes the OKAY/FAIL status has already been read
std::string RecvAdbResponse(const SharedFD& sock) {
  auto length_as_hex_str = RecvAll(sock, kAdbMessageLengthLength);
  if (!IsHexInteger(length_as_hex_str)) {
    LOG(ERROR) << "invalid adb response prefix: " << length_as_hex_str;
    return {};
  }
  auto length = std::stoi(length_as_hex_str, nullptr, 16);
  return RecvAll(sock, length);
}

// Returns a negative value if uptime result couldn't be read for
// any reason.
int RecvUptimeResult(const SharedFD& sock) {
  std::vector<char> uptime_vec{};
  std::vector<char> just_read(16);
  do {
    auto count = sock->Read(just_read.data(), just_read.size());
    if (count < 0) {
      LOG(WARNING) << "couldn't receive adb shell output";
      return -1;
    }
    just_read.resize(count);
    uptime_vec.insert(uptime_vec.end(), just_read.begin(), just_read.end());
  } while (!just_read.empty());

  if (uptime_vec.empty()) {
    LOG(WARNING) << "empty adb shell result";
    return -1;
  }

  uptime_vec.pop_back();

  auto uptime_str = std::string{uptime_vec.data(), uptime_vec.size()};
  if (!IsInteger(uptime_str)) {
    LOG(WARNING) << "non-numeric: uptime result: " << uptime_str;
    return -1;
  }

  return std::stoi(uptime_str);
}

// Returns a negative value if getstatus result couldn't be read for
// any reason.
int RecvGetStatusResult(const SharedFD& sock) {
  std::vector<char> status_vec{};
  std::vector<char> just_read(16);
  do {
    auto count = sock->Read(just_read.data(), just_read.size());
    if (count < 0) {
      LOG(WARNING) << "couldn't receive adb shell output";
      return -1;
    }
    just_read.resize(count);
    status_vec.insert(status_vec.end(), just_read.begin(), just_read.end());
  } while (!just_read.empty());

  if (status_vec.empty()) {
    LOG(WARNING) << "empty adb shell result";
    return -1;
  }

  auto status_str = std::string{status_vec.data(), status_vec.size()};
  LOG(DEBUG) << "Status received " << status_str;

  return 0;
}

// Check if the connection state is waiting for authorization. This function
// returns true only when explicitly receiving the unauthorized error message,
// while returns false for all the other error cases because we need to call
// AdbConnect() again rather than waiting for users' authorization.
bool WaitForAdbAuthorization(const std::string& address) {
  auto sock = SharedFD::SocketLocalClient(kAdbDaemonPort, SOCK_STREAM);
  // Socket doesn't open, so we should not block at waiting for authorization.
  if (!sock->IsOpen()) {
    LOG(WARNING) << "failed to open adb connection: " << sock->StrError();
    return false;
  }

  if (!SendAll(sock, MakeGetStateMessage(address))) {
    LOG(WARNING) << "failed to send get state message to adb daemon";
    return false;
  }

  const std::string status = RecvAll(sock, kAdbStatusResponseLength);
  // Stop waiting because the authorization check passed.
  if (status == kAdbOkayStatusResponse) {
    return false;
  }

  const auto response = RecvAdbResponse(sock);
  // Do not wait for authorization due to failure to receive an adb response.
  if (response.empty()) {
    return false;
  }

  return android::base::StartsWith(response, kAdbUnauthorizedMsg);
}

// There needs to be a gap between the adb commands, the daemon isn't able to
// handle the avalanche of requests we would be sending without a sleep. Five
// seconds is much larger than seems necessary so we should be more than okay.
static constexpr int kAdbCommandGapTime = 5;

void EstablishConnection(const std::string& address) {
  LOG(DEBUG) << "Attempting to connect to device with address " << address;
  while (!AdbConnect(address)) {
    sleep(kAdbCommandGapTime);
  }
  LOG(DEBUG) << "adb connect message for " << address << " successfully sent";
  sleep(kAdbCommandGapTime);

  while (WaitForAdbAuthorization(address)) {
    LOG(WARNING) << "adb unauthorized, retrying";
    sleep(kAdbCommandGapTime);
  }
  LOG(DEBUG) << "adb connected to " << address;
  sleep(kAdbCommandGapTime);
}

void WaitForAdbDisconnection(const std::string& address) {
  // adb daemon doesn't seem to handle quick, successive messages well. The
  // sleeps stabilize the communication.
  LOG(DEBUG) << "Watching for disconnect on " << address;
  while (true) {
    // First try uptime
    auto sock = SharedFD::SocketLocalClient(kAdbDaemonPort, SOCK_STREAM);
    if (!sock->IsOpen()) {
      LOG(ERROR) << "failed to open adb connection: " << sock->StrError();
      break;
    }
    if (!AdbSendMessage(sock, MakeTransportMessage(address))) {
      LOG(WARNING) << "transport message failed, response body: "
                   << RecvAdbResponse(sock);
      break;
    }

    if (AdbSendMessage(sock, MakeShellUptimeMessage())) {
      auto uptime = RecvUptimeResult(sock);
      if (uptime < 0) {
        LOG(WARNING) << "couldn't read uptime result";
        break;
      }
      LOG(VERBOSE) << "device on " << address << " uptime " << uptime;
    } else {
      // If uptime fails, maybe we are in trade-in mode
      // Try adb shell tradeinmode getstatus
      auto sock = SharedFD::SocketLocalClient(kAdbDaemonPort, SOCK_STREAM);
      if (!AdbSendMessage(sock, MakeTransportMessage(address))) {
        LOG(WARNING) << "transport message failed, response body: "
                     << RecvAdbResponse(sock);
        break;
      }
      if (!AdbSendMessage(sock, MakeShellTradeInModeGetStatusMessage())) {
        LOG(WARNING) << "transport message failed, response body: "
                     << RecvAdbResponse(sock);
        break;
      }
      auto status = RecvGetStatusResult(sock);
      if (status < 0) {
        LOG(WARNING) << "transport message failed, response body: "
                     << RecvAdbResponse(sock);
        break;
      }
    }
    sleep(kAdbCommandGapTime);
  }
  LOG(DEBUG) << "Sending adb disconnect";
  AdbDisconnect(address);
  sleep(kAdbCommandGapTime);
}

}  // namespace

[[noreturn]] void EstablishAndMaintainConnection(const std::string& address) {
  while (true) {
    EstablishConnection(address);
    WaitForAdbDisconnection(address);
  }
}

}  // namespace cuttlefish
