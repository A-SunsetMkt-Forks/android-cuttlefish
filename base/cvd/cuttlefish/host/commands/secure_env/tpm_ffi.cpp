//
// Copyright (C) 2020 The Android Open Source Project
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

#include "tpm_ffi.h"

#include <android-base/logging.h>

#include "cuttlefish/host/commands/secure_env/tpm_hmac.h"
#include "cuttlefish/host/commands/secure_env/tpm_resource_manager.h"

using cuttlefish::TpmResourceManager;

extern "C" {

uint32_t tpm_hmac(void* trm, const uint8_t* data, uint32_t data_len,
                  uint8_t* tag, uint32_t tag_len) {
  if (trm == nullptr) {
    LOG(ERROR) << "No TPM resource manager provided";
    return 1;
  }
  TpmResourceManager* resource_manager =
      reinterpret_cast<TpmResourceManager*>(trm);
  auto hmac =
      TpmHmacWithContext(*resource_manager, "TpmHmac_context", data, data_len);
  if (!hmac) {
    LOG(ERROR) << "Could not calculate HMAC";
    return 1;
  } else if (hmac->size != tag_len) {
    LOG(ERROR) << "HMAC size of " << hmac->size
               << " different than expected tag len " << tag_len;
    return 1;
  }
  memcpy(tag, hmac->buffer, tag_len);
  return 0;
}

void secure_env_log(const char* file, unsigned int line, int severity,
                    const char* tag, const char* msg) {
  android::base::LogSeverity severity_enum;
  switch (severity) {
    case 0:
      severity_enum = android::base::LogSeverity::VERBOSE;
      break;
    case 1:
      severity_enum = android::base::LogSeverity::DEBUG;
      break;
    case 2:
      severity_enum = android::base::LogSeverity::INFO;
      break;
    case 3:
      severity_enum = android::base::LogSeverity::WARNING;
      break;
    default:
    case 4:
      severity_enum = android::base::LogSeverity::ERROR;
      break;
    case 5:
      severity_enum = android::base::LogSeverity::FATAL_WITHOUT_ABORT;
      break;
    case 6:
      severity_enum = android::base::LogSeverity::FATAL;
      break;
  }
  android::base::LogMessage::LogLine(file, line, severity_enum, tag, msg);
}
}
