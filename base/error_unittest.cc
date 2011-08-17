// Copyright 2007-2009 Google Inc.
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


#include "omaha/base/error.h"
#include "omaha/testing/unit_test.h"

namespace omaha {

TEST(ErrorTest, HRESULTFromLastError) {
  ::SetLastError(ERROR_ACCESS_DENIED);
  EXPECT_EQ(HRESULTFromLastError(), E_ACCESSDENIED);

  ::SetLastError(static_cast<DWORD>(E_INVALIDARG));
  EXPECT_EQ(HRESULTFromLastError(), E_INVALIDARG);
}

TEST(ErrorTest, HRESULTFromLastErrorAssert) {
  ExpectAsserts expect_asserts;
  ::SetLastError(0);
  EXPECT_EQ(HRESULTFromLastError(), E_FAIL);
}

}  // namespace omaha

