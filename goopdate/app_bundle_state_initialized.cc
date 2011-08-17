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

#include "omaha/goopdate/app_bundle_state_initialized.h"
#include "omaha/base/debug.h"
#include "omaha/base/error.h"
#include "omaha/base/logging.h"
#include "omaha/common/app_registry_utils.h"
#include "omaha/common/config_manager.h"
#include "omaha/common/web_services_client.h"
#include "omaha/goopdate/app_bundle_state_busy.h"
#include "omaha/goopdate/app_bundle_state_paused.h"
#include "omaha/goopdate/app_bundle_state_stopped.h"
#include "omaha/goopdate/app_manager.h"
#include "omaha/goopdate/model.h"

namespace omaha {

namespace fsm {

HRESULT AppBundleStateInitialized::Pause(AppBundle* app_bundle) {
  CORE_LOG(L3, (_T("[AppBundleStateInitialized::Pause][0x%p]"), app_bundle));
  ASSERT1(app_bundle);
  ASSERT1(app_bundle->model()->IsLockedByCaller());

  ChangeState(app_bundle, new AppBundleStatePaused);
  return S_OK;
}

HRESULT AppBundleStateInitialized::Stop(AppBundle* app_bundle) {
  CORE_LOG(L3, (_T("[AppBundleStateInitialized::Stop][0x%p]"), app_bundle));
  ASSERT1(app_bundle);
  ASSERT1(app_bundle->model()->IsLockedByCaller());

  ChangeState(app_bundle, new AppBundleStateStopped);
  return S_OK;
}

// Remains in this state.
HRESULT AppBundleStateInitialized::CreateApp(AppBundle* app_bundle,
                                             const CString& app_id,
                                             App** app) {
  CORE_LOG(L3, (_T("[AppBundleStateInitialized::CreateApp][0x%p]"),
                app_bundle));
  ASSERT1(app_bundle);
  ASSERT1(app);
  ASSERT1(app_bundle->model()->IsLockedByCaller());

  // TODO(omaha): consider enabling this runtime test. Currently, there are
  // a few unit tests that break this assumption mostly during the setup of
  // the unit test itself.
#if 0
  if (app_id.CompareNoCase(kGoogleUpdateAppId) == 0) {
    CORE_LOG(LE, (_T("[Omaha itself can't be created as a new app]")));
    return E_INVALIDARG;
  }
#endif

  if (has_installed_app_) {
    CORE_LOG(LE, (_T("[CreateApp][Installed app already in bundle]")));
    return HandleInvalidStateTransition(app_bundle, _T(__FUNCTION__));
  }

  GUID app_guid = {0};
  HRESULT hr = StringToGuidSafe(app_id, &app_guid);
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[invalid app id][%s]"), app_id));
    return hr;
  }

  scoped_ptr<App> local_app(new App(app_guid, false, app_bundle));
  hr = AddApp(app_bundle, local_app.get());
  if (FAILED(hr)) {
    return hr;
  }

  // When overinstalling, we want the install age for the existing install, so
  // explicitly get it here. This is the only value read from the registry for
  // installs.
  AppManager::Instance()->ReadAppInstallTimeDiff(local_app.get());

  *app = local_app.release();
  has_new_app_ = true;
  return S_OK;
}

// Remains in this state.
HRESULT AppBundleStateInitialized::CreateInstalledApp(AppBundle* app_bundle,
                                                      const CString& app_id,
                                                      App** app) {
  CORE_LOG(L3, (_T("[AppBundleStateInitialized::CreateInstalledApp][0x%p]"),
                app_bundle));
  ASSERT1(app_bundle->model()->IsLockedByCaller());

  if (has_new_app_) {
    CORE_LOG(LE, (_T("[CreateInstalledApp][New app already in bundle]")));
    return HandleInvalidStateTransition(app_bundle, _T(__FUNCTION__));
  }

  // Make sure that the application registration is up to date.
  HRESULT hr = AppManager::Instance()->RunRegistrationUpdateHook(app_id);
  if (FAILED(hr)) {
    CORE_LOG(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) == hr ? L3 : LW,
             (_T("[RunRegistrationUpdateHook failed][%s][0x%x]"),
             app_id, hr));
  }

  hr = AddInstalledApp(app_bundle, app_id, app);
  if (FAILED(hr)) {
    return hr;
  }

  return S_OK;
}

// Remains in this state.
// This function must explicitly check to ensure duplicate apps are not added
// because AddInstalledApp errors are ignored. The check for an empty bundle
// also covers the has_new_app_ case.
HRESULT AppBundleStateInitialized::CreateAllInstalledApps(
    AppBundle* app_bundle) {
  CORE_LOG(L3, (_T("[AppBundleStateInitialized::CreateAllInstalledApps][0x%p]"),
                app_bundle));
  ASSERT1(app_bundle->model()->IsLockedByCaller());

  if (app_bundle->GetNumberOfApps() > 0) {
    CORE_LOG(LE, (_T("[CreateAllInstalledApps][Bundle already has apps]")));
    return HandleInvalidStateTransition(app_bundle, _T(__FUNCTION__));
  }
  ASSERT1(!has_new_app_);

  // Make sure the list of installed applications is up to date. This is
  // primarily important for Google Pack, which supports updating third-party
  // applications that are not aware of Omaha registration, and hence will not
  // update the registration during an install or uninstall outside of Pack.
  AppManager& app_manager = *AppManager::Instance();
  HRESULT hr = app_manager.RunAllRegistrationUpdateHooks();
  if (FAILED(hr)) {
    CORE_LOG(LW, (_T("[RunAllRegistrationUpdateHooks failed][0x%x]"), hr));
  }

  AppIdVector registered_app_ids;
  hr = app_manager.GetRegisteredApps(&registered_app_ids);
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[GetRegisteredApps failed][0x%08x]"), hr));
    return hr;
  }

  for (size_t i = 0; i != registered_app_ids.size(); ++i) {
    const CString& app_id = registered_app_ids[i];

    ASSERT(RegKey::HasKey(app_registry_utils::GetAppClientStateKey(
                              app_bundle->is_machine(), app_id)),
           (_T("[Clients key without matching ClientState][%s]"), app_id));

    App* app = NULL;
    HRESULT hr = AddInstalledApp(app_bundle, app_id, &app);
    if (FAILED(hr)) {
      CORE_LOG(LW, (_T("[AddInstalledApp failed processing app][%s]"), app_id));
    }
  }

  return S_OK;
}

// It is important that the lock is held for the entirety of this and similar
// methods with asynchronous callbacks because CompleteAsyncCall() must not be
// called before the state has been changed to busy.
HRESULT AppBundleStateInitialized::CheckForUpdate(AppBundle* app_bundle) {
  CORE_LOG(L3, (_T("[AppBundleStateInitialized::CheckForUpdate][0x%p]"),
                app_bundle));
  ASSERT1(app_bundle);
  ASSERT1(app_bundle->model()->IsLockedByCaller());
  ASSERT1(!IsPendingNonBlockingCall(app_bundle));

  if (app_bundle->GetNumberOfApps() == 0) {
    CORE_LOG(LE, (_T("[CheckForUpdate][No apps in bundle]")));
    return HandleInvalidStateTransition(app_bundle, _T(__FUNCTION__));
  }

  ASSERT1(has_new_app_ != has_installed_app_);

  HRESULT hr = app_bundle->model()->CheckForUpdate(app_bundle);
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[CheckForUpdates failed][0x%x][0x%p]"), hr, app_bundle));
    return hr;
  }

  ChangeState(app_bundle, new AppBundleStateBusy);
  return S_OK;
}

HRESULT AppBundleStateInitialized::UpdateAllApps(AppBundle* app_bundle) {
  CORE_LOG(L3, (_T("[AppBundleStateInitialized::UpdateAllApps][0x%p]"),
                app_bundle));
  ASSERT1(app_bundle);
  ASSERT1(app_bundle->model()->IsLockedByCaller());
  ASSERT1(!IsPendingNonBlockingCall(app_bundle));

  if (app_bundle->GetNumberOfApps() != 0) {
    CORE_LOG(LE, (_T("[UpdateAllApps][Apps already in bundle]")));
    return HandleInvalidStateTransition(app_bundle, _T(__FUNCTION__));
  }

  app_bundle->set_is_auto_update(true);

  HRESULT hr = app_bundle->createAllInstalledApps();
  if (FAILED(hr)) {
    return hr;
  }
  ASSERT1(app_bundle->GetNumberOfApps() > 0);

  hr = app_bundle->model()->UpdateAllApps(app_bundle);
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[UpdateAllApps failed][0x%08x][0x%p]"), hr, app_bundle));
    return hr;
  }

  ChangeState(app_bundle, new AppBundleStateBusy);
  return S_OK;
}

HRESULT AppBundleStateInitialized::DownloadPackage(
    AppBundle* app_bundle,
    const CString& app_id,
    const CString& package_name) {
  CORE_LOG(L3, (_T("[AppBundleStateInitialized::DownloadPackage][0x%p]"),
                app_bundle));
  ASSERT1(app_bundle);
  ASSERT1(app_bundle->model()->IsLockedByCaller());

  if (app_bundle->GetNumberOfApps() == 0 || has_new_app_) {
    CORE_LOG(LE, (_T("[DownloadPackage][No existing apps in bundle]")));
    return HandleInvalidStateTransition(app_bundle, _T(__FUNCTION__));
  }

  return DoDownloadPackage(app_bundle, app_id, package_name);
}

// App is created with is_update=true because using an installed app's
// information, including a non-zero version, is an update.
HRESULT AppBundleStateInitialized::AddInstalledApp(AppBundle* app_bundle,
                                                   const CString& app_id,
                                                   App** app) {
  ASSERT1(app_bundle);
  ASSERT1(app);
  ASSERT1(app_bundle->model()->IsLockedByCaller());

  GUID app_guid = {0};
  HRESULT hr = StringToGuidSafe(app_id, &app_guid);
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[invalid app id][%s]"), app_id));
    return hr;
  }

  scoped_ptr<App> local_app(new App(app_guid, true, app_bundle));

  hr = AppManager::Instance()->ReadAppPersistentData(local_app.get());
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[ReadAppPersistentData failed][0x%x][%s]"), hr, app_id));
    return hr;
  }

  hr = AddApp(app_bundle, local_app.get());
  if (FAILED(hr)) {
    return hr;
  }

  has_installed_app_ = true;
  *app = local_app.release();
  return S_OK;
}

// Fails if the app already exists in the bundle.
HRESULT AppBundleStateInitialized::AddApp(AppBundle* app_bundle, App* app) {
  ASSERT1(app_bundle);
  ASSERT1(app);
  ASSERT1(app_bundle->model()->IsLockedByCaller());

  for (size_t i = 0; i != app_bundle->GetNumberOfApps(); ++i) {
    App* existing_app = app_bundle->GetApp(i);
    if (::IsEqualGUID(existing_app->app_guid(), app->app_guid())) {
      CORE_LOG(LE, (_T("[App already in bundle][%s]"), app->app_guid_string()));
      return GOOPDATE_E_CALL_UNEXPECTED;
    }
  }

  AddAppToBundle(app_bundle, app);
  return S_OK;
}

}  // namespace fsm

}  // namespace omaha
