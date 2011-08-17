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

#ifndef OMAHA_GOOPDATE_APP_MANAGER_H_
#define OMAHA_GOOPDATE_APP_MANAGER_H_

#include <windows.h>
#include <atlstr.h>
#include <vector>
#include "base/basictypes.h"
#include "omaha/base/synchronized.h"

namespace omaha {

class App;
class RegKey;

typedef std::vector<CString> AppIdVector;

// Manages the persistence of application state in the registry.
// All functions that operate on model objects assume the call is protected by
// the model lock.
// All public functions hold a registry access lock for the duration of registry
// accesses in that function. Unless otherwise noted, read operations may return
// inconsistent/unstable state in some cases. Examples include:
// * The app installer is running and modifying the registry (not all installers
//   acquire this lock before modifying the registry).
// * Omaha is in the process of installing an app, and the read occurred between
//   registry operations (i.e. after WritePreInstallData() but before
//   WriteAppPersistentData().
// If your operation absolutely needs consistent/stable state, use the functions
// that ensure this.
// All write functions assume that the lock returned by
// GetRegistryStableStateLock() is held. Reads do not require this lock to be
// held.
class AppManager {
 public:
  // These values are a public API. Do not remove or move existing values.
  enum InstallerResult {
    INSTALLER_RESULT_SUCCESS = 0,
    INSTALLER_RESULT_FAILED_CUSTOM_ERROR = 1,
    INSTALLER_RESULT_FAILED_MSI_ERROR = 2,
    INSTALLER_RESULT_FAILED_SYSTEM_ERROR = 3,
    INSTALLER_RESULT_EXIT_CODE = 4,
    INSTALLER_RESULT_DEFAULT = INSTALLER_RESULT_EXIT_CODE,
    INSTALLER_RESULT_MAX,
  };

  static HRESULT CreateInstance(bool is_machine);
  static void DeleteInstance();

  static AppManager* Instance();

  // Reads the "pv" value from Google\Update\Clients\{app_guid}, and is used by
  // the Update3WebControl. This method does not take any locks, and is not
  // recommended for use in any other scenario.
  static HRESULT ReadAppVersionNoLock(bool is_machine, const GUID& app_guid,
                                      CString* version);

  bool IsAppRegistered(const GUID& app_guid) const;
  bool IsAppUninstalled(const GUID& app_guid) const;

  // Adds all registered products to bundle.
  HRESULT GetRegisteredApps(AppIdVector* app_ids) const;

  // Adds all uninstalled products to bundle.
  HRESULT GetUninstalledApps(AppIdVector* app_ids) const;

  // Adds all OEM installed apps that user has accepted EULA either explicitly
  // or implicitly.
  HRESULT GetOemInstalledAndEulaAcceptedApps(AppIdVector* app_ids) const;

  // TODO(omaha3): Consider moving these two RegistrationUpdateHook functions
  // out of this class. Instead, AppManager should expose a function to obtain
  // the hook for each app.

  // CoCreates and runs the HookClsid for app_id.
  HRESULT RunRegistrationUpdateHook(const CString& app_id) const;

  // CoCreates and runs HookClsids for registered products.
  HRESULT RunAllRegistrationUpdateHooks() const;

  // Populates the app object with the persisted state stored in the registry.
  HRESULT ReadAppPersistentData(App* app);

  // Populates the app object with the install time diff based on the install
  // time stored in the registry.
  // If the app is registered or has pv value, app's install time diff will be
  // calculated based on InstallTime value from the registry, or 0 if the value
  // is not there. For other cases, the install time diff will be -1 day.
  void ReadAppInstallTimeDiff(App* app);

  // Populates the app object with the persisted state for an uninstalled app
  // stored in the registry.
  HRESULT ReadUninstalledAppPersistentData(App* app);

  // Sets dynamic install parameters that the installer or app may use.
  // Call this method before calling the installer.
  HRESULT WritePreInstallData(const App& app);

  // Reads Installer Result API values the installer may have written to the
  // registry. Clears all values after reading.
  void ReadInstallerResultApiValues(const GUID& app_guid,
                                    InstallerResult* installer_result,
                                    DWORD* installer_error,
                                    DWORD* installer_extra_code1,
                                    CString* installer_result_uistring,
                                    CString* installer_success_launch_cmd);

  // Clears the Installer Result API values from the registry.
  void ClearInstallerResultApiValues(const GUID& app_guid);

  // Reads the values the app wrote to the Clients key and stores them in the
  // app object. Replaces existing values.
  HRESULT ReadInstallerRegistrationValues(App* app);

  // Updates relevant values of the app object in the registry after a
  // successful update check, which is either a "noupdate" response or an update
  // available even if it will not be applied.
  void PersistSuccessfulUpdateCheckResponse(const App& app,
                                            bool is_update_available);

  // Persists relevant values of the app object in the registry after a
  // successful install.
  void PersistSuccessfulInstall(const App& app);

  // Copies product version and language from client key to client state key.
  // Returns S_OK when the client key does not exist.
  HRESULT SynchronizeClientState(const GUID& app_guid);

  // Updates application state after an update check request has been
  // successfully sent to the server.
  HRESULT PersistUpdateCheckSuccessfullySent(
      const App& app,
      int elapsed_seconds_since_day_start);

  // TODO(omaha3): Most of these methods should be eliminated or moved (i.e. to
  // App) since we only want to write the registry in one or two functions.
  // Can't make them all private in the meantime because unit tests use them.

  // Clears the OEM-installed flag for the apps.
  void ClearOemInstalled(const AppIdVector& app_ids);

  // Obtains usage stats information from the stored information about update
  // available events for the app.
  void ReadUpdateAvailableStats(const GUID& app_guid,
                                DWORD* update_responses,
                                DWORD64* time_since_first_response_ms);

  // Removes the ClientState and ClientStateMedium keys for the application.
  HRESULT RemoveClientState(const GUID& app_guid);

  // Returns a reference to the lock that ensures the registry is in a stable
  // state (i.e. no app is being installed). Acquire this lock before calling
  // read functions if you require a consistent/stable snapshot of the system
  // (for example, to determine whether Omaha should install). Because this
  // lock is held throughout app install, the Lock() call could block for
  // seconds or more.
  Lockable& GetRegistryStableStateLock() { return registry_stable_state_lock_; }

  // Gets the time since InstallTime was written. Returns 0 if InstallTime
  // could not be read. This could occur if the app is not already installed or
  // there is no valid install time in the registry, which can occur for apps
  // installed before installtime was implemented.
  uint32 GetInstallTimeDiffSec(const GUID& app_guid) const;

#if 0
  HRESULT RegisterProduct(const GUID& product_guid,
                          const CString& product_name);
  HRESULT UnregisterProduct(const GUID& product_guid);
#endif

  bool IsAppRegistered(const CString& app_id) const;
  bool IsAppUninstalled(const CString& app_id) const;
  bool IsAppOemInstalledAndEulaAccepted(const CString& app_id) const;

 private:
  explicit AppManager(bool is_machine);
  ~AppManager() {}

  bool InitializeRegistryLock();

  CString GetClientKeyName(const GUID& app_guid) const;
  CString GetClientStateKeyName(const GUID& app_guid) const;
  CString GetClientStateMediumKeyName(const GUID& app_guid) const;

  // Opens the app's Client key for read access.
  HRESULT OpenClientKey(const GUID& app_guid, RegKey* client_key) const;
  // Opens the app's ClientState key with the specified access.
  HRESULT OpenClientStateKey(const GUID& app_guid,
                             REGSAM sam_desired,
                             RegKey* client_state_key) const;
  // Creates the app's ClientState key.
  HRESULT CreateClientStateKey(const GUID& app_guid, RegKey* client_state_key);

  // Write the TT Token with what the server returned.
  HRESULT SetTTToken(const App& app);

  // Stores information about the update available event for the app.
  // Call each time an update is available.
  void UpdateUpdateAvailableStats(const GUID& app_guid);

  HRESULT ClearInstallationId(const App& app);

  // Writes the day start time when last active ping/roll call happened to
  // registry.
  void SetLastPingDayStartTime(const App& app,
                               int elapsed_seconds_since_day_start);

  bool IsRegistryStableStateLockedByCaller() const {
    return ::GetCurrentThreadId() == registry_stable_state_lock_.GetOwner();
  }

  const bool is_machine_;

  // Locks.
  // If it is going to be acquired, registry_stable_state_lock_ should always be
  // acquired before registry_access_lock_.
  // registry_access_lock_ is only ever acquired by this class and app
  // installers.

  // Ensures that each function's access is on a stable snapshot of the
  // registry, excluding values modified by the installer.
  GLock registry_access_lock_;

  // Ensures the registry is in a stable state (i.e. all apps are fully
  // installed and no installer is running that might be modifying the
  // registry.) Uninstalls are still an issue unless the app uninstaller informs
  // Omaha that it is uninstalling the app.
  LLock registry_stable_state_lock_;

  static AppManager* instance_;

  friend class RunRegistrationUpdateHooksFunc;
  friend class AppManagerTestBase;

  DISALLOW_COPY_AND_ASSIGN(AppManager);
};

}  // namespace omaha

#endif  // OMAHA_GOOPDATE_APP_MANAGER_H_
