// Copyright 2009-2010 Google Inc.
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

#include "omaha/goopdate/app_state_waiting_to_check_for_update.h"
#include "omaha/base/debug.h"
#include "omaha/base/error.h"
#include "omaha/base/highres_timer-win32.h"
#include "omaha/base/logging.h"
#include "omaha/common/update_request.h"
#include "omaha/goopdate/app_manager.h"
#include "omaha/goopdate/app_state_checking_for_update.h"
#include "omaha/goopdate/update_request_utils.h"
#include "omaha/goopdate/model.h"
#include "omaha/goopdate/server_resource.h"
#include "omaha/goopdate/string_formatter.h"
#include "omaha/goopdate/worker.h"
#include "omaha/goopdate/worker_metrics.h"

namespace omaha {

namespace fsm {

AppStateWaitingToCheckForUpdate::AppStateWaitingToCheckForUpdate()
    : AppState(STATE_WAITING_TO_CHECK_FOR_UPDATE) {
}

void AppStateWaitingToCheckForUpdate::PreUpdateCheck(
    App* app,
    xml::UpdateRequest* update_request) {
  CORE_LOG(L3, (_T("[AppStateWaitingToCheckForUpdate::PreUpdateCheck]")));
  ASSERT1(app);
  ASSERT1(update_request);

  ASSERT1(app->model()->IsLockedByCaller());

  const CString& current_version(app->current_version()->version());
  if (!current_version.IsEmpty()) {
    app->model()->PurgeAppLowerVersions(app->app_guid_string(),
                                        current_version);
  }

  AppManager* app_manager(AppManager::Instance());

  VERIFY1(SUCCEEDED(app_manager->SynchronizeClientState(app->app_guid())));

  // Handle the normal flow and return. Abnormal cases are below.
  if (app->is_eula_accepted()) {
    update_request_utils::BuildRequest(app, true, update_request);
    ChangeState(app, new AppStateCheckingForUpdate);
    return;
  }

  // The app's EULA has not been accepted, so do not add this app to the update
  // check. This means bundle size does not always match the request size.

  ASSERT1(app->app_guid() != kGoopdateGuid);

  // TODO(omaha3): Is there a better way to do this such that we don't need to
  // know about offline installs here?
  if (app->app_bundle()->is_offline_install()) {
    // Offline installs do not need requests, so skip building the request.
    ChangeState(app, new AppStateCheckingForUpdate);
    return;
  }

  ASSERT1(app->is_update());
  metric_worker_apps_not_updated_eula++;

  StringFormatter formatter(app->app_bundle()->display_language());
  CString message;
  VERIFY1(SUCCEEDED(formatter.LoadString(IDS_INSTALL_FAILED, &message)));
  Error(app,
        ErrorContext(GOOPDATE_E_APP_UPDATE_DISABLED_EULA_NOT_ACCEPTED),
        message);
}

}  // namespace fsm

}  // namespace omaha
