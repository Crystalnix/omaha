// Copyright 2010 Google Inc.
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

#include "omaha/goopdate/app_bundle_state_stopped.h"
#include "omaha/base/logging.h"

namespace omaha {

namespace fsm {

HRESULT AppBundleStateStopped::Stop(AppBundle* app_bundle) {
  UNREFERENCED_PARAMETER(app_bundle);
  CORE_LOG(L3, (_T("[AppBundleStateStopped::Stop][0x%p]"), app_bundle));
  return S_OK;
}

HRESULT AppBundleStateStopped::CompleteAsyncCall(AppBundle* app_bundle) {
  UNREFERENCED_PARAMETER(app_bundle);
  CORE_LOG(L3, (_T("[AppBundleStateStopped::CompleteAsyncCall][0x%p]"),
      app_bundle));
  return S_OK;
}

}  // namespace fsm

}  // namespace omaha

