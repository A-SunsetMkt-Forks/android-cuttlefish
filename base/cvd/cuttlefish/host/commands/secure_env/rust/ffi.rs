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

//! KeyMint TA core for Cuttlefish.

extern crate alloc;

use kmr_wire::keymint::SecurityLevel;
use libc::c_int;
use log::error;
use std::os::fd::OwnedFd;

/// FFI wrapper around [`kmr_cf::ta_main`].
///
/// # Safety
///
/// `fd_in`, `fd_out`, and `snapshot_socket_fd` must be valid and open file descriptors and the
/// caller must not use or close them after the call.
///
/// TODO: What are the preconditions for `trm`?
#[no_mangle]
pub unsafe extern "C" fn kmr_ta_main(
    fd_in: OwnedFd,
    fd_out: OwnedFd,
    security_level: c_int,
    trm: *mut libc::c_void,
    snapshot_socket_fd: OwnedFd,
) {
    let security_level = match SecurityLevel::n(security_level) {
        Some(
            x @ (SecurityLevel::Software
            | SecurityLevel::TrustedEnvironment
            | SecurityLevel::Strongbox),
        ) => x,
        _ => {
            error!("unexpected security level {}, running as SOFTWARE", security_level);
            SecurityLevel::Software
        }
    };
    // SAFETY: TODO: What are the preconditions for `trm`?
    unsafe {
        kmr_cf::ta_main(fd_in.into(), fd_out.into(), security_level, trm, snapshot_socket_fd.into())
    }
}
