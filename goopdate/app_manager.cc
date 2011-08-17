// Copyright 2007-2010 Google Inc.
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

#include "omaha/goopdate/app_manager.h"
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>
#include "base/scoped_ptr.h"
#include "omaha/base/const_object_names.h"
#include "omaha/base/error.h"
#include "omaha/base/reg_key.h"
#include "omaha/base/scoped_ptr_address.h"
#include "omaha/base/time.h"
#include "omaha/base/utils.h"
#include "omaha/base/vistautil.h"
#include "omaha/common/app_registry_utils.h"
#include "omaha/common/config_manager.h"
#include "omaha/common/const_goopdate.h"
#include "omaha/common/oem_install_utils.h"
#include "omaha/goopdate/application_usage_data.h"
#include "omaha/goopdate/model.h"
#include "omaha/goopdate/server_resource.h"
#include "omaha/goopdate/string_formatter.h"

namespace omaha {

namespace {

const uint32 kInitialInstallTimeDiff = static_cast<uint32>(-1 * kSecondsPerDay);

// Returns the number of days haven been passed since the given time.
// The parameter time is in the same format as C time() returns.
int GetNumberOfDaysSince(int time) {
  ASSERT1(time >= 0);
  const int now = Time64ToInt32(GetCurrent100NSTime());
  ASSERT1(now >= time);

  if (now < time) {
    // In case the client computer clock is adjusted in between.
    return 0;
  }
  return (now - time) / kSecondsPerDay;
}

// Determines if an application is registered with Omaha.
class IsAppRegisteredFunc
    : public std::unary_function<const CString&, HRESULT> {
 public:
  explicit IsAppRegisteredFunc(const CString& guid)
      : is_registered_(false),
        guid_(guid) {}

  bool is_registered() const { return is_registered_; }

  HRESULT operator() (const CString& guid) {
    if (guid.CompareNoCase(guid_) == 0) {
      is_registered_ = true;
    }
    return S_OK;
  }
 private:
  CString guid_;
  bool is_registered_;
};

// Enumerates all sub keys of the key and calls the functor for each of them,
// ignoring errors to ensure all keys are processed.
template <typename T>
HRESULT EnumerateSubKeys(const TCHAR* key_name, T* functor) {
  RegKey client_key;
  HRESULT hr = client_key.Open(key_name, KEY_READ);
  if (FAILED(hr)) {
    return hr;
  }

  int num_sub_keys = client_key.GetSubkeyCount();
  for (int i = 0; i < num_sub_keys; ++i) {
    CString sub_key_name;
    hr = client_key.GetSubkeyNameAt(i, &sub_key_name);
    if (SUCCEEDED(hr)) {
      (*functor)(sub_key_name);
    }
  }

  return S_OK;
}

}  // namespace

typedef bool (*AppPredictFunc)(const AppManager& app_manager,
                               const CString& app_id);

bool IsUninstalledAppPredicate(const AppManager& app_manager,
                               const CString& app_id) {
  return app_manager.IsAppUninstalled(app_id);
}

bool IsAppOemInstalledAndEulaAcceptedPredicate(const AppManager& app_manager,
                                               const CString& app_id) {
  return app_manager.IsAppOemInstalledAndEulaAccepted(app_id);
}

bool IsRegisteredAppPredicate(const AppManager& app_manager,
                              const CString& app_id) {
  return app_manager.IsAppRegistered(app_id);
}

// Accumulates app IDs for apps that satisfies the predicate.
class CollectProductsFunc
    : public std::unary_function<const CString&, HRESULT> {
 public:
  CollectProductsFunc(const AppPredictFunc predicate,
                      const AppManager& app_manager,
                      AppIdVector* app_ids)
      : predicate_(predicate),
        app_manager_(app_manager),
        app_ids_(app_ids) {
    ASSERT1(app_ids);
  }

  // Ignores errors and accumulates as many applications as possible.
  HRESULT operator() (const CString& app_id) const {
    if ((*predicate_)(app_manager_, app_id)) {
      app_ids_->push_back(app_id);
    }

    return S_OK;
  }

 private:
  const AppPredictFunc predicate_;
  const AppManager& app_manager_;
  AppIdVector* const app_ids_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(CollectProductsFunc);
};

// Runs application registration hooks registered under Omaha AppIds.
// Reads the Hook Clsid entry under Clients\{AppID}. CoCreates the CLSID. Calls
// IRegistrationUpdateHook::UpdateRegistry().
class RunRegistrationUpdateHooksFunc
    : public std::unary_function<const CString&, HRESULT> {
 public:
  explicit RunRegistrationUpdateHooksFunc(const AppManager& app_manager)
      : app_manager_(app_manager) {
  }

  HRESULT operator() (const CString& app_id) {
    GUID app_guid = GUID_NULL;
    HRESULT hr = StringToGuidSafe(app_id, &app_guid);
    if (FAILED(hr)) {
      return hr;
    }
    RegKey client_key;
    hr = app_manager_.OpenClientKey(app_guid, &client_key);
    if (FAILED(hr)) {
      return hr;
    }

    CString hook_clsid_str;
    hr = client_key.GetValue(kRegValueUpdateHookClsid, &hook_clsid_str);
    if (FAILED(hr)) {
      return hr;
    }
    GUID hook_clsid = GUID_NULL;
    hr = StringToGuidSafe(hook_clsid_str, &hook_clsid);
    if (FAILED(hr)) {
      return hr;
    }

    CORE_LOG(L3, (_T("[Update Hook Clsid][%s][%s]"), app_id, hook_clsid_str));

    CComPtr<IRegistrationUpdateHook> registration_hook;
    hr = registration_hook.CoCreateInstance(hook_clsid);
    if (FAILED(hr)) {
      CORE_LOG(LE, (_T("[IRegistrationUpdateHook CoCreate failed][0x%x]"), hr));
      return hr;
    }

    hr = registration_hook->UpdateRegistry(CComBSTR(app_id),
                                           app_manager_.is_machine_);
    if (FAILED(hr)) {
      CORE_LOG(LE, (_T("[registration_hook UpdateRegistry failed][0x%x]"), hr));
      return hr;
    }

    return S_OK;
  }

 private:
  const AppManager& app_manager_;
};

AppManager* AppManager::instance_ = NULL;

// We do not worry about contention on creation because only the Worker should
// create AppManager during its initialization.
HRESULT AppManager::CreateInstance(bool is_machine) {
  ASSERT1(!instance_);
  if (instance_) {
    return S_OK;
  }

  AppManager* instance(new AppManager(is_machine));
  if (!instance->InitializeRegistryLock()) {
    HRESULT hr(HRESULTFromLastError());
    delete instance;
    return hr;
  }

  instance_ = instance;
  return S_OK;
}

void AppManager::DeleteInstance() {
  delete instance_;
  instance_ = NULL;
}

AppManager* AppManager::Instance() {
  ASSERT1(instance_);
  return instance_;
}

HRESULT AppManager::ReadAppVersionNoLock(bool is_machine, const GUID& app_guid,
                                         CString* version) {
  ASSERT1(version);
  CORE_LOG(L2, (_T("[ReadAppVersionNoLock][%s]"), GuidToString(app_guid)));

  AppManager app_manager(is_machine);
  RegKey client_key;
  HRESULT hr = app_manager.OpenClientKey(app_guid, &client_key);
  if (FAILED(hr)) {
    return hr;
  }

  hr = client_key.GetValue(kRegValueProductVersion, version);
  if (FAILED(hr)) {
    return hr;
  }

  CORE_LOG(L3, (_T("[kRegValueProductVersion][%s]"), *version));
  return S_OK;
}

AppManager::AppManager(bool is_machine)
    : is_machine_(is_machine) {
  CORE_LOG(L3, (_T("[AppManager::AppManager][is_machine=%d]"), is_machine));
}

// App installers should use similar code to create a lock to acquire while
// modifying Omaha registry.
bool AppManager::InitializeRegistryLock() {
  NamedObjectAttributes lock_attr;
  GetNamedObjectAttributes(kRegistryAccessMutex, is_machine_, &lock_attr);
  return registry_access_lock_.InitializeWithSecAttr(lock_attr.name,
                                                     &lock_attr.sa);
}

// Vulnerable to a race condition with installers. To prevent this, acquire
// GetRegistryStableStateLock().
bool AppManager::IsAppRegistered(const GUID& app_guid) const {
  return IsAppRegistered(GuidToString(app_guid));
}

// Vulnerable to a race condition with installers. To prevent this, acquire
// GetRegistryStableStateLock().
bool AppManager::IsAppRegistered(const CString& app_id) const {
  IsAppRegisteredFunc func(app_id);
  HRESULT hr = EnumerateSubKeys(
      ConfigManager::Instance()->registry_clients(is_machine_),
      &func);
  if (FAILED(hr)) {
    return false;
  }

  return func.is_registered();
}

bool AppManager::IsAppUninstalled(const CString& app_id) const {
  GUID app_guid = {0};
  if (FAILED(StringToGuidSafe(app_id, &app_guid))) {
    ASSERT1(false);
    return false;
  }
  return IsAppUninstalled(app_guid);
}

// An app is considered uninstalled if:
//  * The app's Clients key does not exist AND
//  * The app's ClientState key exists and contains the pv value.
// We check for the pv key value in the ClientState to prevent Omaha from
// detecting the key created in the following scenarios as an uninstalled app.
//  * Per-machine apps may write dr to per-user Omaha's key. Per-user Omaha
//    must not detect this as an uninstalled app.
//  * Omaha may create the app's ClientState key and write values from the
//    metainstaller tag before running the installer, which creates the
//    Clients key.
bool AppManager::IsAppUninstalled(const GUID& app_guid) const {
  if (IsAppRegistered(app_guid)) {
    return false;
  }

  return RegKey::HasValue(GetClientStateKeyName(app_guid),
                          kRegValueProductVersion);
}

bool AppManager::IsAppOemInstalledAndEulaAccepted(const CString& app_id) const {
  GUID app_guid = GUID_NULL;
  if (FAILED(StringToGuidSafe(app_id, &app_guid))) {
    ASSERT1(false);
    return false;
  }

  if (IsAppUninstalled(app_guid)) {
    return false;
  }

  if (!app_registry_utils::IsAppEulaAccepted(is_machine_, app_id, false)) {
    CORE_LOG(L3, (_T("[EULA not accepted for app %s, its OEM ping not sent.]"),
                  app_id.GetString()));
    return false;
  }

  return RegKey::HasValue(GetClientStateKeyName(app_guid), kRegValueOemInstall);
}

// Vulnerable to a race condition with installers. To prevent this, hold
// GetRegistryStableStateLock() while calling this function and related
// functions, such as ReadAppPersistentData().
HRESULT AppManager::GetRegisteredApps(AppIdVector* app_ids) const {
  ASSERT1(app_ids);

  CollectProductsFunc func(IsRegisteredAppPredicate, *this, app_ids);

  return EnumerateSubKeys(
      ConfigManager::Instance()->registry_clients(is_machine_),
      &func);
}

// Vulnerable to a race condition with installers. To prevent this, acquire
// GetRegistryStableStateLock().
HRESULT AppManager::GetUninstalledApps(AppIdVector* app_ids) const {
  ASSERT1(app_ids);

  CollectProductsFunc func(IsUninstalledAppPredicate, *this, app_ids);

  return EnumerateSubKeys(
      ConfigManager::Instance()->registry_client_state(is_machine_),
      &func);
}

HRESULT AppManager::GetOemInstalledAndEulaAcceptedApps(
    AppIdVector* app_ids) const {
  ASSERT1(app_ids);

  CollectProductsFunc func(IsAppOemInstalledAndEulaAcceptedPredicate,
                           *this,
                           app_ids);

  return EnumerateSubKeys(
      ConfigManager::Instance()->registry_client_state(is_machine_),
      &func);
}

HRESULT AppManager::RunRegistrationUpdateHook(const CString& app_id) const {
  return RunRegistrationUpdateHooksFunc(*this)(app_id);
}

// Vulnerable to a race condition with installers. We think this is acceptable.
// If there is a future requirement for greater consistency, acquire
// GetRegistryStableStateLock().
HRESULT AppManager::RunAllRegistrationUpdateHooks() const {
  RunRegistrationUpdateHooksFunc func(*this);
  const TCHAR* key(ConfigManager::Instance()->registry_clients(is_machine_));
  return EnumerateSubKeys(key, &func);
}

CString AppManager::GetClientKeyName(const GUID& app_guid) const {
  return app_registry_utils::GetAppClientsKey(is_machine_,
                                              GuidToString(app_guid));
}

CString AppManager::GetClientStateKeyName(const GUID& app_guid) const {
  return app_registry_utils::GetAppClientStateKey(is_machine_,
                                                  GuidToString(app_guid));
}

CString AppManager::GetClientStateMediumKeyName(const GUID& app_guid) const {
  ASSERT1(is_machine_);
  return app_registry_utils::GetAppClientStateMediumKey(is_machine_,
                                                        GuidToString(app_guid));
}

// Assumes the registry access lock is held.
HRESULT AppManager::OpenClientKey(const GUID& app_guid,
                                  RegKey* client_key) const {
  ASSERT1(client_key);
  return client_key->Open(GetClientKeyName(app_guid), KEY_READ);
}

// Assumes the registry access lock is held.
HRESULT AppManager::OpenClientStateKey(const GUID& app_guid,
                                       REGSAM sam_desired,
                                       RegKey* client_state_key) const {
  ASSERT1(client_state_key);
  CString key_name = GetClientStateKeyName(app_guid);
  return client_state_key->Open(key_name, sam_desired);
}

// Also creates the ClientStateMedium key for machine apps, ensuring it exists
// whenever ClientState exists.  Does not create ClientStateMedium for Omaha.
// This function is called for self-updates, so it must explicitly avoid this.
// Assumes the registry access lock is held.
HRESULT AppManager::CreateClientStateKey(const GUID& app_guid,
                                         RegKey* client_state_key) {
  ASSERT1(client_state_key);
  // TODO(omaha3): Add GetOwner() to GLock & add this to Open() functions too.
  // ASSERT1(::GetCurrentThreadId() == registry_access_lock_.GetOwner());

  const CString key_name = GetClientStateKeyName(app_guid);
  HRESULT hr = client_state_key->Create(key_name);
  if (FAILED(hr)) {
    CORE_LOG(L3, (_T("[RegKey::Create failed][0x%08x]"), hr));
    return hr;
  }

  if (!is_machine_) {
    return S_OK;
  }

  if (::IsEqualGUID(kGoopdateGuid, app_guid)) {
    return S_OK;
  }

  const CString medium_key_name = GetClientStateMediumKeyName(app_guid);
  hr = RegKey::CreateKey(medium_key_name);
  if (FAILED(hr)) {
    CORE_LOG(L3, (_T("[RegKey::Create ClientStateMedium failed][0x%08x]"), hr));
    return hr;
  }

  return S_OK;
}

// Reads the following values from the registry:
//  Clients key
//    pv
//    lang
//    name
//  ClientState key
//    lang (if not present in Clients)
//    ap
//    tttoken
//    iid
//    brand
//    client
//    experiment
//    (referral is intentionally not read)
//    InstallTime (converted to diff)
//    oeminstall
//  ClientState and ClientStateMedium key
//    eulaaccepted
//  ClientState key in HKCU/HKLM/Low integrity
//    did run
//
// app_guid_ is set to the app_guid argument.
// Note: pv is not read from ClientState into app_data. It's
// presence is checked for an uninstall
// TODO(omaha3): We will need to get ClientState's pv when reporting uninstalls.
// Note: If the application is uninstalled, the Clients key may not exist.
HRESULT AppManager::ReadAppPersistentData(App* app) {
  ASSERT1(app);

  const GUID& app_guid = app->app_guid();
  const CString& app_guid_string = app->app_guid_string();

  CORE_LOG(L2, (_T("[AppManager::ReadAppPersistentData][%s]"),
                app_guid_string));

  ASSERT1(app->model()->IsLockedByCaller());

  __mutexScope(registry_access_lock_);

  const bool is_eula_accepted =
      app_registry_utils::IsAppEulaAccepted(is_machine_,
                                            app_guid_string,
                                            false);
  app->is_eula_accepted_ = is_eula_accepted ? TRISTATE_TRUE : TRISTATE_FALSE;

  bool client_key_exists = false;
  RegKey client_key;
  HRESULT hr = OpenClientKey(app_guid, &client_key);
  if (SUCCEEDED(hr)) {
    client_key_exists = true;

    CString version;
    hr = client_key.GetValue(kRegValueProductVersion, &version);
    CORE_LOG(L3, (_T("[AppManager::ReadAppPersistentData]")
                  _T("[%s][version=%s]"), app_guid_string, version));
    if (FAILED(hr)) {
      return hr;
    }

    app->current_version()->set_version(version);

    // Language and name might not be written by installer, so ignore failures.
    client_key.GetValue(kRegValueLanguage, &app->language_);
    client_key.GetValue(kRegValueAppName, &app->display_name_);
  }

  // Ensure there is a valid display name.
  if (app->display_name_.IsEmpty()) {
    StringFormatter formatter(app->app_bundle()->display_language());

    CString company_name;
    VERIFY1(SUCCEEDED(formatter.LoadString(IDS_FRIENDLY_COMPANY_NAME,
                                           &company_name)));

    VERIFY1(SUCCEEDED(formatter.FormatMessage(&app->display_name_,
                                              IDS_DEFAULT_APP_DISPLAY_NAME,
                                              company_name)));
  }

  // If ClientState registry key doesn't exist, the function could return.
  // Before opening the key, set days_since_last* to -1, which is the
  // default value if reg key doesn't exist. If later we find that the values
  // are readable, new values will overwrite current ones.
  app->set_days_since_last_active_ping(-1);
  app->set_days_since_last_roll_call(-1);

  // The following do not rely on client_state_key, so check them before
  // possibly returning if OpenClientStateKey fails.

  // Reads the did run value.
  ApplicationUsageData app_usage(is_machine_, vista_util::IsVistaOrLater());
  app_usage.ReadDidRun(app_guid_string);

  // Sets did_run regardless of the return value of ReadDidRun above. If read
  // fails, active_state() should return ACTIVE_UNKNOWN which is intented.
  app->did_run_ = app_usage.active_state();

  // TODO(omaha3): Consider moving GetInstallTimeDiffSec() up here. Be careful
  // that the results when ClientState does not exist are desirable. See the
  // comments near that function and above set_days_since_last_active_ping call.

  RegKey client_state_key;
  hr = OpenClientStateKey(app_guid, KEY_READ, &client_state_key);
  if (FAILED(hr)) {
    // It is possible that the client state key has not yet been populated.
    // In this case just return the information that we have gathered thus far.
    // However if both keys do not exist, then we are doing something wrong.
    CORE_LOG(LW, (_T("[AppManager::ReadAppPersistentData - No ClientState]")));
    if (client_key_exists) {
      return S_OK;
    } else {
      return hr;
    }
  }

  // Read language from ClientState key if it was not found in the Clients key.
  if (app->language().IsEmpty()) {
    client_state_key.GetValue(kRegValueLanguage, &app->language_);
  }

  client_state_key.GetValue(kRegValueAdditionalParams, &app->ap_);
  client_state_key.GetValue(kRegValueTTToken, &app->tt_token_);

  CString iid;
  client_state_key.GetValue(kRegValueInstallationId, &iid);
  GUID iid_guid;
  if (SUCCEEDED(StringToGuidSafe(iid, &iid_guid))) {
    app->iid_ = iid_guid;
  }

  client_state_key.GetValue(kRegValueBrandCode, &app->brand_code_);
  ASSERT1(app->brand_code_.GetLength() <= kBrandIdLength);
  client_state_key.GetValue(kRegValueClientId, &app->client_id_);

  // We do not need the referral_id.

  DWORD last_active_ping_sec(0);
  if (SUCCEEDED(client_state_key.GetValue(kRegValueActivePingDayStartSec,
                                          &last_active_ping_sec))) {
    int days_since_last_active_ping =
        GetNumberOfDaysSince(static_cast<int32>(last_active_ping_sec));
    app->set_days_since_last_active_ping(days_since_last_active_ping);
  }

  DWORD last_roll_call_sec(0);
  if (SUCCEEDED(client_state_key.GetValue(kRegValueRollCallDayStartSec,
                                          &last_roll_call_sec))) {
    int days_since_last_roll_call =
        GetNumberOfDaysSince(static_cast<int32>(last_roll_call_sec));
    app->set_days_since_last_roll_call(days_since_last_roll_call);
  }

  app->install_time_diff_sec_ = GetInstallTimeDiffSec(app_guid);
  // Generally GetInstallTimeDiffSec() shouldn't return kInitialInstallTimeDiff
  // here. The only exception is in the unexpected case when ClientState exists
  // without a pv.
  ASSERT1((app->install_time_diff_sec_ != kInitialInstallTimeDiff) ||
          !RegKey::HasValue(GetClientStateKeyName(app_guid),
                            kRegValueProductVersion));

  return S_OK;
}

void AppManager::ReadAppInstallTimeDiff(App* app) {
  ASSERT1(app);
  app->install_time_diff_sec_ = GetInstallTimeDiffSec(app->app_guid());
}

// Calls ReadAppPersistentData() to populate app and adds the following values
// specific to uninstalled apps:
//  ClientState key
//    pv:  set as current_version()->version
//
// Since this is an uninstalled app, values from the Clients key should not be
// populated.
HRESULT AppManager::ReadUninstalledAppPersistentData(App* app) {
  ASSERT1(app);
  ASSERT1(!IsAppRegistered(app->app_guid_string()));

  HRESULT hr = ReadAppPersistentData(app);
  if (FAILED(hr)) {
    return hr;
  }

  ASSERT1(app->current_version()->version().IsEmpty());

  RegKey client_state_key;
  hr = OpenClientStateKey(app->app_guid(), KEY_READ, &client_state_key);
  ASSERT(SUCCEEDED(hr), (_T("Uninstalled apps have a ClientState key.")));

  CString version;
  hr = client_state_key.GetValue(kRegValueProductVersion, &version);
  CORE_LOG(L3, (_T("[AppManager::ReadAppPersistentData]")
                _T("[%s][uninstalled version=%s]"),
                app->app_guid_string(), version));
  ASSERT(SUCCEEDED(hr), (_T("Uninstalled apps have a pv.")));
  app->current_version()->set_version(version);

  return S_OK;
}

// Sets the following values in the app's ClientState, to make them available to
// the installer:
//    lang
//    ap
//    brand (in SetAppBranding)
//    client (in SetAppBranding)
//    experiment
//    referral (in SetAppBranding)
//    InstallTime (in SetAppBranding; converted from diff)
//    oeminstall (if appropriate)
//    eulaaccepted (set/deleted)
//    browser
//    usagestats
// Sets eulaaccepted=0 if the app is not already registered and the app's EULA
// has not been accepted. Deletes eulaaccepted if the EULA has been accepted.
// Only call for initial or over-installs. Do not call for updates to avoid
// mistakenly replacing data, such as the application's language, and causing
// unexpected changes to the app during a silent update.
HRESULT AppManager::WritePreInstallData(const App& app) {
  CORE_LOG(L2, (_T("[AppManager::WritePreInstallData][%s]"),
                app.app_guid_string()));

  ASSERT1(app.app_bundle()->is_machine() == is_machine_);

  ASSERT1(IsRegistryStableStateLockedByCaller());
  __mutexScope(registry_access_lock_);

  RegKey client_state_key;
  HRESULT hr = CreateClientStateKey(app.app_guid(), &client_state_key);
  if (FAILED(hr)) {
    return hr;
  }

  if (app.is_eula_accepted()) {
    hr = app_registry_utils::ClearAppEulaNotAccepted(is_machine_,
                                                     app.app_guid_string());
  } else {
    if (!IsAppRegistered(app.app_guid())) {
      hr = app_registry_utils::SetAppEulaNotAccepted(is_machine_,
                                                     app.app_guid_string());
    }
  }
  if (FAILED(hr)) {
    return hr;
  }

  if (!app.language().IsEmpty()) {
    VERIFY1(SUCCEEDED(client_state_key.SetValue(kRegValueLanguage,
                                                app.language())));
  }

  if (app.ap().IsEmpty()) {
    VERIFY1(SUCCEEDED(client_state_key.DeleteValue(kRegValueAdditionalParams)));
  } else {
    VERIFY1(SUCCEEDED(client_state_key.SetValue(kRegValueAdditionalParams,
                                                app.ap())));
  }

  CString state_key_path = GetClientStateKeyName(app.app_guid());
  VERIFY1(SUCCEEDED(app_registry_utils::SetAppBranding(state_key_path,
                                                       app.brand_code(),
                                                       app.client_id(),
                                                       app.referral_id())));

  if (app.GetExperimentLabels().IsEmpty()) {
    VERIFY1(SUCCEEDED(client_state_key.DeleteValue(kRegValueExperimentLabels)));
  } else {
    VERIFY1(SUCCEEDED(client_state_key.SetValue(kRegValueExperimentLabels,
                                                app.GetExperimentLabels())));
  }

  if (oem_install_utils::IsOemInstalling(is_machine_)) {
    ASSERT1(is_machine_);
    VERIFY1(SUCCEEDED(client_state_key.SetValue(kRegValueOemInstall, _T("1"))));
  }

  if (BROWSER_UNKNOWN == app.browser_type()) {
    VERIFY1(SUCCEEDED(client_state_key.DeleteValue(kRegValueBrowser)));
  } else {
    DWORD browser_type = app.browser_type();
    VERIFY1(SUCCEEDED(client_state_key.SetValue(kRegValueBrowser,
                                                browser_type)));
  }

  if (TRISTATE_NONE != app.usage_stats_enable()) {
    VERIFY1(SUCCEEDED(app_registry_utils::SetUsageStatsEnable(
                          is_machine_,
                          app.app_guid_string(),
                          app.usage_stats_enable())));
  }

  return S_OK;
}

// All values are optional.
void AppManager::ReadInstallerResultApiValues(
    const GUID& app_guid,
    InstallerResult* installer_result,
    DWORD* installer_error,
    DWORD* installer_extra_code1,
    CString* installer_result_uistring,
    CString* installer_success_launch_cmd) {
  ASSERT1(installer_result);
  ASSERT1(installer_error);
  ASSERT1(installer_extra_code1);
  ASSERT1(installer_result_uistring);
  ASSERT1(installer_success_launch_cmd);

  __mutexScope(registry_access_lock_);

  RegKey client_state_key;
  HRESULT hr = OpenClientStateKey(app_guid, KEY_READ, &client_state_key);
  if (FAILED(hr)) {
    return;
  }

  if (SUCCEEDED(client_state_key.GetValue(
                    kRegValueInstallerResult,
                    reinterpret_cast<DWORD*>(installer_result)))) {
    CORE_LOG(L1, (_T("[InstallerResult in registry][%u]"), *installer_result));
  }
  if (*installer_result >= INSTALLER_RESULT_MAX) {
    CORE_LOG(LW, (_T("[Unsupported InstallerResult value]")));
    *installer_result = INSTALLER_RESULT_DEFAULT;
  }

  if (SUCCEEDED(client_state_key.GetValue(kRegValueInstallerError,
                                          installer_error))) {
    CORE_LOG(L1, (_T("[InstallerError in registry][%u]"), *installer_error));
  }

  if (SUCCEEDED(client_state_key.GetValue(kRegValueInstallerExtraCode1,
                                          installer_extra_code1))) {
    CORE_LOG(L1, (_T("[InstallerExtraCode1 in registry][%u]"),
        *installer_extra_code1));
  }

  if (SUCCEEDED(client_state_key.GetValue(kRegValueInstallerResultUIString,
                                          installer_result_uistring))) {
    CORE_LOG(L1, (_T("[InstallerResultUIString in registry][%s]"),
        *installer_result_uistring));
  }

  if (SUCCEEDED(client_state_key.GetValue(
                    kRegValueInstallerSuccessLaunchCmdLine,
                    installer_success_launch_cmd))) {
    CORE_LOG(L1, (_T("[InstallerSuccessLaunchCmdLine in registry][%s]"),
        *installer_success_launch_cmd));
  }

  ClearInstallerResultApiValues(app_guid);
}

void AppManager::ClearInstallerResultApiValues(const GUID& app_guid) {
  const CString client_state_key_name = GetClientStateKeyName(app_guid);
  const CString update_key_name =
      ConfigManager::Instance()->registry_update(is_machine_);

  ASSERT1(IsRegistryStableStateLockedByCaller());
  __mutexScope(registry_access_lock_);

  // Delete the old LastXXX values.  These may not exist, so don't care if they
  // fail.
  RegKey::DeleteValue(client_state_key_name,
                      kRegValueLastInstallerResult);
  RegKey::DeleteValue(client_state_key_name,
                      kRegValueLastInstallerResultUIString);
  RegKey::DeleteValue(client_state_key_name,
                      kRegValueLastInstallerError);
  RegKey::DeleteValue(client_state_key_name,
                      kRegValueLastInstallerExtraCode1);
  RegKey::DeleteValue(client_state_key_name,
                      kRegValueLastInstallerSuccessLaunchCmdLine);

  // Also delete any values from Google\Update.
  // TODO(Omaha): This is a temporary fix for bug 1539293. See TODO below.
  RegKey::DeleteValue(update_key_name,
                      kRegValueLastInstallerResult);
  RegKey::DeleteValue(update_key_name,
                      kRegValueLastInstallerResultUIString);
  RegKey::DeleteValue(update_key_name,
                      kRegValueLastInstallerError);
  RegKey::DeleteValue(update_key_name,
                      kRegValueLastInstallerExtraCode1);
  RegKey::DeleteValue(update_key_name,
                      kRegValueLastInstallerSuccessLaunchCmdLine);

  // Rename current InstallerResultXXX values to LastXXX.
  RegKey::RenameValue(client_state_key_name,
                      kRegValueInstallerResult,
                      kRegValueLastInstallerResult);
  RegKey::RenameValue(client_state_key_name,
                      kRegValueInstallerError,
                      kRegValueLastInstallerError);
  RegKey::RenameValue(client_state_key_name,
                      kRegValueInstallerExtraCode1,
                      kRegValueLastInstallerExtraCode1);
  RegKey::RenameValue(client_state_key_name,
                      kRegValueInstallerResultUIString,
                      kRegValueLastInstallerResultUIString);
  RegKey::RenameValue(client_state_key_name,
                      kRegValueInstallerSuccessLaunchCmdLine,
                      kRegValueLastInstallerSuccessLaunchCmdLine);

  // Copy over to the Google\Update key.
  // TODO(Omaha3): This is a temporary fix for bug 1539293. Once Pack V2 is
  // deprecated (Pack stops taking offline installers for new versions of
  // Omaha apps), remove this. (It might be useful to leave the CopyValue calls
  // in DEBUG builds only.)
  RegKey::CopyValue(client_state_key_name,
                    update_key_name,
                    kRegValueLastInstallerResult);
  RegKey::CopyValue(client_state_key_name,
                    update_key_name,
                    kRegValueLastInstallerError);
  RegKey::CopyValue(client_state_key_name,
                    update_key_name,
                    kRegValueLastInstallerExtraCode1);
  RegKey::CopyValue(client_state_key_name,
                    update_key_name,
                    kRegValueLastInstallerResultUIString);
  RegKey::CopyValue(client_state_key_name,
                    update_key_name,
                    kRegValueLastInstallerSuccessLaunchCmdLine);
}

// Reads the following values from Clients:
//    pv
//    lang (if present)
// name is not read. TODO(omaha3): May change if we persist name in registry.
HRESULT AppManager::ReadInstallerRegistrationValues(App* app) {
  ASSERT1(app);

  const CString& app_guid_string = app->app_guid_string();

  CORE_LOG(L2, (_T("[AppManager::ReadInstallerRegistrationValues][%s]"),
                app_guid_string));

  ASSERT1(app->model()->IsLockedByCaller());

  __mutexScope(registry_access_lock_);

  RegKey client_key;
  if (FAILED(OpenClientKey(app->app_guid(), &client_key))) {
    OPT_LOG(LE, (_T("[Installer did not create key][%s]"), app_guid_string));
    return GOOPDATEINSTALL_E_INSTALLER_DID_NOT_WRITE_CLIENTS_KEY;
  }

  CString version;
  if (FAILED(client_key.GetValue(kRegValueProductVersion, &version))) {
    OPT_LOG(LE, (_T("[Installer did not register][%s]"), app_guid_string));
    return GOOPDATEINSTALL_E_INSTALLER_DID_NOT_WRITE_CLIENTS_KEY;
  }

  if (version.IsEmpty()) {
    OPT_LOG(LE, (_T("[Installer did not write version][%s]"), app_guid_string));
    return GOOPDATEINSTALL_E_INSTALLER_DID_NOT_WRITE_CLIENTS_KEY;
  }

  app->next_version()->set_version(version);

  CString language;
  if (SUCCEEDED(client_key.GetValue(kRegValueLanguage, &language))) {
    app->language_ = language;
  }

  return S_OK;
}

// Writes tttoken and updates relevant stats.
void AppManager::PersistSuccessfulUpdateCheckResponse(
    const App& app,
    bool is_update_available) {
  CORE_LOG(L2, (_T("[AppManager::PersistSuccessfulUpdateCheckResponse]")
                _T("[%s][%d]"), app.app_guid_string(), is_update_available));
  __mutexScope(registry_access_lock_);

  VERIFY1(SUCCEEDED(SetTTToken(app)));

  const CString client_state_key = GetClientStateKeyName(app.app_guid());

  if (is_update_available) {
    if (app.error_code() == GOOPDATE_E_APP_UPDATE_DISABLED_BY_POLICY) {
      // The error indicates is_update and updates are disabled by policy.
      ASSERT1(app.is_update());
      app_registry_utils::ClearUpdateAvailableStats(client_state_key);
    } else if (app.is_update()) {
      // Only record an update available event for updates.
      // We have other mechanisms, including IID, to track install success.
      UpdateUpdateAvailableStats(app.app_guid());
    }
  } else {
    app_registry_utils::ClearUpdateAvailableStats(client_state_key);
    app_registry_utils::PersistSuccessfulUpdateCheck(client_state_key);
  }
}

// Writes the following values to the ClientState key:
//    pv (should be value written by installer in Clients key)
//    lang (should be value written by installer in Clients key)
//    iid (set/deleted)
//
// Does not write the following values because they were set by
// WritePreInstallData() and would not have changed during installation unless
// modified directly by the app installer.
//    ap
//    brand
//    client
//    experiment
//    referral
//    InstallTime (converted from diff)
//    oeminstall
//    eulaaccepted
//    browser
//    usagestats
// TODO(omaha3): Maybe we should delete referral at this point. Ask Chrome.
//
// Other values, such as tttoken were set after the update check.
//
// The caller is responsible for modifying the values in app_data as
// appropriate, including:
//   * Updating values in app_data to reflect installer's values (pv and lang)
//   * Clearing iid if appropriate.
//   * Clearing the did run value. TODO(omaha3): Depends on TODO below.
void AppManager::PersistSuccessfulInstall(const App& app) {
  CORE_LOG(L2, (_T("[AppManager::PersistSuccessfulInstall][%s]"),
                app.app_guid_string()));

  ASSERT1(IsRegistryStableStateLockedByCaller());
  __mutexScope(registry_access_lock_);

  ASSERT1(!::IsEqualGUID(kGoopdateGuid, app.app_guid()));

  RegKey client_state_key;
  VERIFY1(SUCCEEDED(CreateClientStateKey(app.app_guid(), &client_state_key)));

  VERIFY1(SUCCEEDED(client_state_key.SetValue(kRegValueProductVersion,
                                              app.next_version()->version())));

  if (!app.language().IsEmpty()) {
    VERIFY1(SUCCEEDED(client_state_key.SetValue(kRegValueLanguage,
                                                app.language())));
  }

  if (::IsEqualGUID(app.iid(), GUID_NULL)) {
    VERIFY1(SUCCEEDED(client_state_key.DeleteValue(kRegValueInstallationId)));
  } else {
    VERIFY1(SUCCEEDED(client_state_key.SetValue(
                          kRegValueInstallationId,
                          GuidToString(app.iid()))));
  }

  const CString client_state_key_path = GetClientStateKeyName(app.app_guid());
  app_registry_utils::PersistSuccessfulInstall(client_state_key_path,
                                               app.is_update(),
                                               false);  // TODO(omaha3): offline
}

HRESULT AppManager::SynchronizeClientState(const GUID& app_guid) {
  __mutexScope(registry_access_lock_);

  RegKey client_key;
  HRESULT hr = OpenClientKey(app_guid, &client_key);
  if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
    return S_OK;
  }
  if (FAILED(hr)) {
    return hr;
  }

  RegKey client_state_key;
  hr = CreateClientStateKey(app_guid, &client_state_key);
  if (FAILED(hr)) {
    return hr;
  }

  CString version;
  client_key.GetValue(kRegValueProductVersion, &version);
  if (FAILED(hr)) {
    return hr;
  }
  hr = client_state_key.SetValue(kRegValueProductVersion, version);
  if (FAILED(hr)) {
    return hr;
  }

  CString language;
  client_key.GetValue(kRegValueLanguage, &language);
  if (!language.IsEmpty()) {
    return client_state_key.SetValue(kRegValueLanguage, language);
  }

  return S_OK;
}

// TODO(omaha3): tttoken is not currently read from the server response.
// TODO(omaha3): When implementing offline, we must make sure that the tttoken
// is not deleted by the offline response processing.
// TODO(omaha3): Having the parser write the server's token to the same member
// that is used for the value from the tag exposes this value to the COM setter.
// It would be nice to avoid that, possibly by only allowing that setter to work
// in certain states.
HRESULT AppManager::SetTTToken(const App& app) {
  CORE_LOG(L3, (_T("[AppManager::SetTTToken][token=%s]"), app.tt_token()));

  __mutexScope(registry_access_lock_);

  RegKey client_state_key;
  HRESULT hr = CreateClientStateKey(app.app_guid(), &client_state_key);
  if (FAILED(hr)) {
    return hr;
  }

  if (app.tt_token().IsEmpty()) {
    return client_state_key.DeleteValue(kRegValueTTToken);
  } else {
    return client_state_key.SetValue(kRegValueTTToken, app.tt_token());
  }
}

void AppManager::ClearOemInstalled(const AppIdVector& app_ids) {
  __mutexScope(registry_access_lock_);

  AppIdVector::const_iterator it;
  for (it = app_ids.begin(); it != app_ids.end(); ++it) {
    ASSERT1(IsAppOemInstalledAndEulaAccepted(*it));
    RegKey state_key;

    GUID app_guid = GUID_NULL;
    HRESULT hr = StringToGuidSafe(*it, &app_guid);
    if (FAILED(hr)) {
      continue;
    }

    hr = OpenClientStateKey(app_guid, KEY_ALL_ACCESS, &state_key);
    if (FAILED(hr)) {
      continue;
    }

    VERIFY1(SUCCEEDED(state_key.DeleteValue(kRegValueOemInstall)));
  }
}

void AppManager::UpdateUpdateAvailableStats(const GUID& app_guid) {
  __mutexScope(registry_access_lock_);

  RegKey state_key;
  HRESULT hr = CreateClientStateKey(app_guid, &state_key);
  if (FAILED(hr)) {
    ASSERT1(false);
    return;
  }

  DWORD update_available_count(0);
  hr = state_key.GetValue(kRegValueUpdateAvailableCount,
                          &update_available_count);
  if (FAILED(hr)) {
    update_available_count = 0;
  }
  ++update_available_count;
  VERIFY1(SUCCEEDED(state_key.SetValue(kRegValueUpdateAvailableCount,
                                       update_available_count)));

  DWORD64 update_available_since_time(0);
  hr = state_key.GetValue(kRegValueUpdateAvailableSince,
                          &update_available_since_time);
  if (FAILED(hr)) {
    // There is no existing value, so this must be the first update notice.
    VERIFY1(SUCCEEDED(state_key.SetValue(kRegValueUpdateAvailableSince,
                                         GetCurrent100NSTime())));

    // TODO(omaha): It would be nice to report the version that we were first
    // told to update to. This is available in UpdateResponse but we do not
    // currently send it down in update responses. If we start using it, add
    // kRegValueFirstUpdateResponseVersion.
  }
}

// Returns 0 for any values that are not found.
void AppManager::ReadUpdateAvailableStats(
    const GUID& app_guid,
    DWORD* update_responses,
    DWORD64* time_since_first_response_ms) {
  ASSERT1(update_responses);
  ASSERT1(time_since_first_response_ms);
  *update_responses = 0;
  *time_since_first_response_ms = 0;

  __mutexScope(registry_access_lock_);

  RegKey state_key;
  HRESULT hr = OpenClientStateKey(app_guid, KEY_READ, &state_key);
  if (FAILED(hr)) {
    CORE_LOG(LW, (_T("[App ClientState key does not exist][%s]"),
                  GuidToString(app_guid)));
    return;
  }

  DWORD update_responses_in_reg(0);
  hr = state_key.GetValue(kRegValueUpdateAvailableCount,
                          &update_responses_in_reg);
  if (SUCCEEDED(hr)) {
    *update_responses = update_responses_in_reg;
  }

  DWORD64 update_available_since_time(0);
  hr = state_key.GetValue(kRegValueUpdateAvailableSince,
                          &update_available_since_time);
  if (SUCCEEDED(hr)) {
    const DWORD64 current_time = GetCurrent100NSTime();
    ASSERT1(update_available_since_time <= current_time);
    const DWORD64 time_since_first_response_in_100ns =
        current_time - update_available_since_time;
    *time_since_first_response_ms =
        time_since_first_response_in_100ns / kMillisecsTo100ns;
  }
}

uint32 AppManager::GetInstallTimeDiffSec(const GUID& app_guid) const {
  if (!IsAppRegistered(app_guid) && !IsAppUninstalled(app_guid)) {
    return kInitialInstallTimeDiff;
  }

  RegKey client_state_key;
  HRESULT hr = OpenClientStateKey(app_guid, KEY_READ, &client_state_key);
  if (FAILED(hr)) {
    return 0;
  }

  DWORD install_time(0);
  DWORD install_time_diff_sec(0);
  if (SUCCEEDED(client_state_key.GetValue(kRegValueInstallTimeSec,
                                          &install_time))) {
    const uint32 now = Time64ToInt32(GetCurrent100NSTime());
    if (0 != install_time && now >= install_time) {
      install_time_diff_sec = now - install_time;
      // TODO(omaha3): Restore this assert. In Omaha 2, this function gets
      // called as part of installation verification and Job::UpdateJob(), so
      // the value can be 0. This will not be the case in Omaha 3.
      // ASSERT1(install_time_diff_sec != 0);
    }
  }

  return install_time_diff_sec;
}

// Clear the Installation ID if at least one of the conditions is true:
// 1) DidRun==yes. First run is the last time we want to use the Installation
//    ID. So delete Installation ID if it is present.
// 2) kMaxLifeOfInstallationIDSec has passed since the app was installed. This
//    is to ensure that Installation ID is cleared even if DidRun is never set.
// 3) The app is Omaha. Always delete Installation ID if it is present
//    because DidRun does not apply.
HRESULT AppManager::ClearInstallationId(const App& app) {
  ASSERT1(app.model()->IsLockedByCaller());
  __mutexScope(registry_access_lock_);

  if (::IsEqualGUID(app.iid(), GUID_NULL)) {
    return S_OK;
  }

  if ((ACTIVE_RUN == app.did_run()) ||
      (kMaxLifeOfInstallationIDSec <= app.install_time_diff_sec()) ||
      (::IsEqualGUID(kGoopdateGuid, app.app_guid()))) {
    CORE_LOG(L1, (_T("[Deleting iid for app][%s]"), app.app_guid_string()));

    RegKey client_state_key;
    HRESULT hr = CreateClientStateKey(app.app_guid(), &client_state_key);
    if (FAILED(hr)) {
      return hr;
    }

    return client_state_key.DeleteValue(kRegValueInstallationId);
  }

  return S_OK;
}

void AppManager::SetLastPingDayStartTime(const App& app,
                                         int elapsed_seconds_since_day_start) {
  ASSERT1(elapsed_seconds_since_day_start >= 0);
  ASSERT1(elapsed_seconds_since_day_start < kMaxTimeSinceMidnightSec);
  ASSERT1(app.model()->IsLockedByCaller());

  __mutexScope(registry_access_lock_);

  int now = Time64ToInt32(GetCurrent100NSTime());

  RegKey client_state_key;
  if (FAILED(CreateClientStateKey(app.app_guid(), &client_state_key))) {
    return;
  }

  bool did_send_active_ping = (app.did_run() == ACTIVE_RUN &&
                               app.days_since_last_active_ping() != 0);
  if (did_send_active_ping) {
    VERIFY1(SUCCEEDED(client_state_key.SetValue(
        kRegValueActivePingDayStartSec,
        static_cast<DWORD>(now - elapsed_seconds_since_day_start))));
  }

  bool did_send_roll_call = (app.days_since_last_roll_call() != 0);
  if (did_send_roll_call) {
    VERIFY1(SUCCEEDED(client_state_key.SetValue(
        kRegValueRollCallDayStartSec,
        static_cast<DWORD>(now - elapsed_seconds_since_day_start))));
  }
}

// Writes the day start time when last active ping/roll call happened to
// registry if the corresponding ping has been sent.
// Removes installation id, if did run = true or if goopdate.
// Clears did run.
HRESULT AppManager::PersistUpdateCheckSuccessfullySent(
    const App& app,
    int elapsed_seconds_since_day_start) {
  ASSERT1(app.model()->IsLockedByCaller());

  ApplicationUsageData app_usage(app.app_bundle()->is_machine(),
                                 vista_util::IsVistaOrLater());
  VERIFY1(SUCCEEDED(app_usage.ResetDidRun(app.app_guid_string())));

  SetLastPingDayStartTime(app, elapsed_seconds_since_day_start);

  // Handle the installation id.
  VERIFY1(SUCCEEDED(ClearInstallationId(app)));

  return S_OK;
}

HRESULT AppManager::RemoveClientState(const GUID& app_guid) {
  CORE_LOG(L2, (_T("[AppManager::RemoveClientState][%s]"),
                GuidToString(app_guid)));
  ASSERT1(IsRegistryStableStateLockedByCaller());
  __mutexScope(registry_access_lock_);

  ASSERT1(!IsAppRegistered(app_guid));

  return app_registry_utils::RemoveClientState(is_machine_,
                                               GuidToString(app_guid));
}

// TODO(omaha3): May not need these
#if 0
// Writes 0.0.0.1 to pv. This value avoids any special cases, such as initial
// install rules, for 0.0.0.0, while being unlikely to be higher than the
// product's actual current version.
HRESULT AppManager::RegisterProduct(const GUID& product_guid,
                                    const CString& product_name) {
  const TCHAR* const kRegisterProductVersion = _T("0.0.0.1");

  __mutexScope(GetRegistryStableStateLock());
  RegKey client_key;
  HRESULT hr = client_key.Create(GetClientKeyName(GUID_NULL, product_guid));
  if (FAILED(hr)) {
    return hr;
  }

  hr = client_key.SetValue(kRegValueProductVersion, kRegisterProductVersion);
  if (FAILED(hr)) {
    return hr;
  }

  // AppName is not a required parameter since it's only used for being able to
  // easily tell what application is there when reading the registry.
  VERIFY1(SUCCEEDED(client_key.SetValue(kRegValueAppName, product_name)));

  return S_OK;
}

HRESULT AppManager::UnregisterProduct(const GUID& product_guid) {
  __mutexScope(GetRegistryStableStateLock());
  return RegKey::DeleteKey(GetClientKeyName(GUID_NULL, product_guid), true);
}
#endif

}  // namespace omaha
