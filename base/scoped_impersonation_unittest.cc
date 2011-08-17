// Copyright 2008-2010 Google Inc.
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

#include <windows.h>
#include "base/basictypes.h"
#include "omaha/base/scoped_any.h"
#include "omaha/base/scoped_impersonation.h"
#include "omaha/base/scoped_ptr_address.h"
#include "omaha/base/vista_utils.h"
#include "omaha/testing/unit_test.h"

namespace omaha {

TEST(ScopedImpersonationTest, ImpersonateLoggedOnUser) {
  scoped_handle token;
  vista::GetLoggedOnUserToken(address(token));
  if (token) {
    scoped_impersonation impersonate_user(get(token));
    EXPECT_EQ(impersonate_user.result(), S_OK);
  }
}

TEST(ScopedImpersonationTest, ImpersonateLoggedOnUserNullHandle) {
  scoped_impersonation impersonate_user(NULL);
  EXPECT_EQ(impersonate_user.result(), S_FALSE);
}


}  // namespace omaha

