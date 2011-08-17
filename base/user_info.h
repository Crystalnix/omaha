// Copyright 2004-2010 Google Inc.
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
// ========================================================================
//
// Class UserInfo: Information related to the current user or other users on
// this machine.
//
// TODO(omaha): seems we can merge this module with user_rights.

#ifndef OMAHA_COMMON_USER_INFO_H__
#define OMAHA_COMMON_USER_INFO_H__

#include <windows.h>
#include <atlsecurity.h>
#include <atlstr.h>

namespace omaha {

namespace user_info {

// Gets the user name, domain, and the SID associated with the access token
// of the current process.
HRESULT GetProcessUser(CString* name, CString* domain, CString* sid);

// Gets SID associated with the access token of the current process.
HRESULT GetProcessUserSid(CSid* sid);

// Gets the user SID associated with the access token of the current thread if
// the thread is impersonating. If the thread is not impersonating, the API
// fails with ERROR_NO_TOKEN.
HRESULT GetThreadUserSid(CString* sid);

// Gets the user SID associated with the access token of the current thread if
// the thread is impersonating. Otherwise, gets the SID associated with the
// access token of the current process.
HRESULT GetEffectiveUserSid(CString* sid);

// TODO(omaha): deprecate weird API.
// Looks at the current user SID and checks if it's the same as the
// LocalSystem user.
HRESULT IsLocalSystemUser(bool* is_local_system,
                          CString* user_sid);     // optional.

// Returns true if the caller's impersonation or process access token user
// is LOCAL_SYSTEM.
bool IsRunningAsSystem();

// Returns true if the current thread is impersonating.
bool IsThreadImpersonating();

}  // namespace user_info

}  // namespace omaha

#endif  // OMAHA_COMMON_USER_INFO_H__
