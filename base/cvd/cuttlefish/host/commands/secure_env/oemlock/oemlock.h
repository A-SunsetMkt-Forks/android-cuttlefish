/*
 * Copyright 2023 The Android Open Source Project
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
 *
 */

#pragma once

#include "cuttlefish/common/libs/utils/result.h"

#include "cuttlefish/host/commands/secure_env/storage/storage.h"

namespace cuttlefish {
namespace oemlock {

/**
 * OEMLock TPM server interface
 *
 * Inspired by OemLock HAL interface:
 * https://cs.android.com/android/platform/superproject/+/master:hardware/interfaces/oemlock/aidl/default/Android.bp
*/
class OemLock {
 public:
  OemLock(secure_env::Storage& storage);

  Result<bool> IsOemUnlockAllowedByCarrier() const;
  Result<bool> IsOemUnlockAllowedByDevice() const;
  Result<bool> IsOemUnlockAllowed() const;
  Result<bool> IsOemLocked() const;
  Result<void> SetOemUnlockAllowedByCarrier(bool allowed);
  Result<void> SetOemUnlockAllowedByDevice(bool allowed);
  // TODO(b/286558252): add ConfirmationUI token to the signature
  Result<void> SetOemLocked(bool locked);

 private:
  secure_env::Storage& storage_;
};

} // namespace oemlock
} // namespace cuttlefish
