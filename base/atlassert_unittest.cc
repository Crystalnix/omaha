// Copyright 2003-2009 Google Inc.
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

#include "omaha/base/debug.h"
#include "omaha/testing/unit_test.h"

namespace omaha {

// Test what happens when we hit an ATLASSERT within ATL code.
// The CComPtr expects the parameter to be 0.
TEST(AtlAssertTest, AtlAssert) {
  ExpectAsserts expect_asserts;
  CComPtr<IUnknown> p(1);
}

}  // namespace omaha
