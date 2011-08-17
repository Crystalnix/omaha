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

#ifndef OMAHA_COMMON_APP_REGISTRY_UTILS_H_
#define OMAHA_COMMON_APP_REGISTRY_UTILS_H_

#include <windows.h>
#include <atlstr.h>
#include <vector>
#include "omaha/base/constants.h"

// Functions that modify the application state in the registry. This file should
// only be included by AppManager, which manages the persisting of all
// application information, ApplicationUsageData for similar reasons, and
// self-install code, which must modify these values directly in some cases.

namespace omaha {

namespace app_registry_utils {

// Returns the application registration path for the specified app.
CString GetAppClientsKey(bool is_machine, const CString& app_guid);

// Returns the application state path for the specified app.
CString GetAppClientStateKey(bool is_machine, const CString& app_guid);

// Returns the medium integrity application state path for the specified app.
CString GetAppClientStateMediumKey(bool is_machine, const CString& app_guid);

// Returns whether the EULA is accepted for the app.
bool IsAppEulaAccepted(bool is_machine,
                       const CString& app_guid,
                       bool require_explicit_acceptance);

// Sets eulaaccepted=0 in the app's ClientState.
HRESULT SetAppEulaNotAccepted(bool is_machine, const CString& app_guid);

// Clears any eulaaccepted=0 values for the app.
HRESULT ClearAppEulaNotAccepted(bool is_machine, const CString& app_guid);

// Determines whether usage stats are enabled for a specific app.
bool AreAppUsageStatsEnabled(bool is_machine, const CString& app_guid);

// Configures Omaha's collection of usage stats and crash reports.
HRESULT SetUsageStatsEnable(bool is_machine,
                            const CString& app_guid,
                            Tristate usage_stats_enable);

// Writes branding information for Google Update in the registry if it does not
// already exist. Otherwise, the information remains unchanged.
// Writes a default Omaha-specific brand code if one is not specified in args.
HRESULT SetGoogleUpdateBranding(const CString& client_state_key_path,
                                const CString& brand_code,
                                const CString& client_id);

// Writes branding information for apps in the registry if it does not
// already exist. Otherwise, the information remains unchanged.
// Writes a default Omaha-specific brand code if one is not specified in args.
HRESULT SetAppBranding(const CString& client_state_key_path,
                       const CString& brand_code,
                       const CString& client_id,
                       const CString& referral_id);

// Updates the application state after a successful install or update.
void PersistSuccessfulInstall(const CString& client_state_key_path,
                              bool is_update,
                              bool is_offline);

// Updates the application state after a successful update check event, which
// is either a "noupdate" response or a successful online update.
void PersistSuccessfulUpdateCheck(const CString& client_state_key_path);

// Clears the stored information about update available events for the app.
// Call when an update has succeeded.
void ClearUpdateAvailableStats(const CString& client_state_key_path);

// Returns the number of clients registered under the "Clients" sub key.
// Does not guarantee a consistent state. Caller should use appropriate locks if
// necessary.
HRESULT GetNumClients(bool is_machine, size_t* num_clients);

// Reads app version from Clients key.
void GetAppVersion(bool is_machine, const CString& app_id, CString* pv);

// Reads persistent data for an application. The parameters can be NULL to
// indicate that value is not required.
void GetClientStateData(bool is_machine,
                        const CString& app_id,
                        CString* pv,
                        CString* ap,
                        CString* lang,
                        CString* brand_code,
                        CString* client_id,
                        CString* iid,
                        CString* experiment_labels);

// Reads all uninstalled apps from the registry.
HRESULT GetUninstalledApps(bool is_machine, std::vector<CString>* app_ids);

// Removes the client state for the given app.
HRESULT RemoveClientState(bool is_machine, const CString& app_guid);

// Removes the client state for the apps.
void RemoveClientStateForApps(bool is_machine,
                              const std::vector<CString>& apps);

// Retrieves experiment labels for an app from the Registry.
HRESULT GetExperimentLabels(bool is_machine, const CString& app_id,
                            CString* labels_out);

// Overwrites the experiment labels for an app in the Registry.
HRESULT SetExperimentLabels(bool is_machine, const CString& app_id,
                            const CString& new_labels);

}  // namespace app_registry_utils

}  // namespace omaha

#endif  // OMAHA_COMMON_APP_REGISTRY_UTILS_H_
