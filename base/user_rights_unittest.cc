// Copyright 2008-2009 Google Inc.
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

#include "omaha/testing/unit_test.h"
#include "omaha/base/user_rights.h"

namespace omaha {

TEST(UserRightsTest, UserIsLoggedOnInteractively) {
  if (IsTestRunByLocalSystem()) {
    return;
  }

  bool is_logged_on(false);
  EXPECT_HRESULT_SUCCEEDED(
    UserRights::UserIsLoggedOnInteractively(&is_logged_on));
  EXPECT_TRUE(is_logged_on);
}

}  // namespace omaha

