// Copyright 2006-2010 Google Inc.
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
// The WDK CRT headers do not define ptrdiff_t, which is used by
// scoped_array.

#ifndef OMAHA_MI_EXE_STUB_PTRDIFF_H_
#define OMAHA_MI_EXE_STUB_PTRDIFF_H_

namespace std {

#ifdef _WIN64
typedef __intr64 ptrdiff_t;
#else
typedef int ptrdiff_t;
#endif

}  // namespace std

#endif  // OMAHA_MI_EXE_STUB_PTRDIFF_H_
