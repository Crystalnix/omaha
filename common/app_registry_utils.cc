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

#include "omaha/common/app_registry_utils.h"
#include "omaha/base/constants.h"
#include "omaha/base/debug.h"
#include "omaha/base/logging.h"
#include "omaha/base/reg_key.h"
#include "omaha/base/utils.h"
#include "omaha/common/config_manager.h"
#include "omaha/common/const_goopdate.h"
#include "omaha/common/experiment_labels.h"

namespace omaha {

namespace app_registry_utils {

CString GetAppClientsKey(bool is_machine, const CString& app_guid) {
  return AppendRegKeyPath(
      ConfigManager::Instance()->registry_clients(is_machine),
      app_guid);
}

CString GetAppClientStateKey(bool is_machine, const CString& app_guid) {
  return AppendRegKeyPath(
      ConfigManager::Instance()->registry_client_state(is_machine),
      app_guid);
}

CString GetAppClientStateMediumKey(bool is_machine, const CString& app_guid) {
  ASSERT1(is_machine);
  UNREFERENCED_PARAMETER(is_machine);
  return AppendRegKeyPath(
      ConfigManager::Instance()->machine_registry_client_state_medium(),
      app_guid);
}

// The EULA is assumed to be accepted unless eualaccepted=0 in the ClientState
// key. For machine apps in this case, eulaccepted=1 in ClientStateMedium also
// indicates acceptance and the value in ClientState is updated.
bool IsAppEulaAccepted(bool is_machine,
                       const CString& app_guid,
                       bool require_explicit_acceptance) {
  const CString state_key = GetAppClientStateKey(is_machine, app_guid);

  DWORD eula_accepted = 0;
  if (SUCCEEDED(RegKey::GetValue(state_key,
                                 kRegValueEulaAccepted,
                                 &eula_accepted))) {
    if (0 != eula_accepted) {
      return true;
    }
  } else {
    if (!require_explicit_acceptance) {
      return true;
    }
  }

  if (!is_machine) {
    return false;
  }

  eula_accepted = 0;
  if (SUCCEEDED(RegKey::GetValue(
                    GetAppClientStateMediumKey(is_machine, app_guid),
                    kRegValueEulaAccepted,
                    &eula_accepted))) {
    if (0 == eula_accepted) {
      return false;
    }
  } else {
    return false;
  }

  VERIFY1(SUCCEEDED(RegKey::SetValue(state_key,
                                     kRegValueEulaAccepted,
                                     eula_accepted)));
  return true;
}

// Does not need to set ClientStateMedium.
HRESULT SetAppEulaNotAccepted(bool is_machine, const CString& app_guid) {
  return RegKey::SetValue(GetAppClientStateKey(is_machine, app_guid),
                          kRegValueEulaAccepted,
                          static_cast<DWORD>(0));
}

// Deletes eulaaccepted from ClientState and ClientStateMedium.
HRESULT ClearAppEulaNotAccepted(bool is_machine, const CString& app_guid) {
  const CString state_key = GetAppClientStateKey(is_machine, app_guid);
  if (RegKey::HasKey(state_key)) {
    HRESULT hr = RegKey::DeleteValue(state_key, kRegValueEulaAccepted);
    if (FAILED(hr)) {
      return hr;
    }
  }

  if (!is_machine) {
    return S_OK;
  }

  const CString state_medium_key =
      GetAppClientStateMediumKey(is_machine, app_guid);
  if (RegKey::HasKey(state_medium_key)) {
    HRESULT hr = RegKey::DeleteValue(state_medium_key, kRegValueEulaAccepted);
    if (FAILED(hr)) {
      return hr;
    }
  }

  return S_OK;
}

// For machine apps, ClientStateMedium takes precedence.
// Does not propogate the ClientStateMedium value to ClientState.
bool AreAppUsageStatsEnabled(bool is_machine, const CString& app_guid) {
  if (is_machine) {
    DWORD stats_enabled = 0;
    if (SUCCEEDED(RegKey::GetValue(GetAppClientStateMediumKey(is_machine,
                                                              app_guid),
                                   kRegValueUsageStats,
                                   &stats_enabled))) {
      return (TRISTATE_TRUE == stats_enabled);
    }
  }

  DWORD stats_enabled = 0;
  if (SUCCEEDED(RegKey::GetValue(GetAppClientStateKey(is_machine, app_guid),
                              kRegValueUsageStats,
                              &stats_enabled))) {
    return (TRISTATE_TRUE == stats_enabled);
  }

  return false;
}

// Does nothing if usage_stats_enable is TRISTATE_NONE.
// For machine apps, clears ClientStateMedium because the app may be reading it
// if present.
HRESULT SetUsageStatsEnable(bool is_machine,
                            const CString& app_guid,
                            Tristate usage_stats_enable) {
  if (TRISTATE_NONE == usage_stats_enable) {
    return S_OK;
  }

  const DWORD stats_enabled = (TRISTATE_TRUE == usage_stats_enable) ? 1 : 0;

  HRESULT hr = RegKey::SetValue(GetAppClientStateKey(is_machine, app_guid),
                                kRegValueUsageStats,
                                stats_enabled);
  if (FAILED(hr)) {
    CORE_LOG(LW, (_T("[Failed to set usagestats][0x%08x]"), hr));
    return hr;
  }

  if (!is_machine) {
    return S_OK;
  }

  const CString state_medium_key =
      GetAppClientStateMediumKey(is_machine, app_guid);
  if (RegKey::HasKey(state_medium_key)) {
    hr = RegKey::DeleteValue(state_medium_key, kRegValueUsageStats);
    if (FAILED(hr)) {
      return hr;
    }
  }

  return S_OK;
}

// Google Update does not have a referral_id. Everything else is the same as for
// apps.
HRESULT SetGoogleUpdateBranding(const CString& client_state_key_path,
                                const CString& brand_code,
                                const CString& client_id) {
  HRESULT hr(SetAppBranding(client_state_key_path,
                            brand_code,
                            client_id,
                            CString()));

  if (FAILED(hr)) {
    return hr;
  }

  RegKey state_key;
  hr = state_key.Open(client_state_key_path);
  if (FAILED(hr)) {
    return hr;
  }

  // Legacy support for older versions that do not write the FirstInstallTime.
  // This code ensures that FirstInstallTime always has a valid non-zero value.
  DWORD install_time(0);
  if (FAILED(state_key.GetValue(kRegValueInstallTimeSec, &install_time)) ||
      !install_time) {
    const DWORD now = Time64ToInt32(GetCurrent100NSTime());
    VERIFY1(SUCCEEDED(state_key.SetValue(kRegValueInstallTimeSec, now)));
    CORE_LOG(L3, (_T("[InstallTime missing. Setting it here.][%u]"), now));
  }

  return S_OK;
}

// Branding information is only written if a brand code is not already present.
// We should only write it if this is the first install of Omaha to avoid giving
// undue credit to a later installer source. Writing a default brand code
// prevents future branded installations from setting their brand.
// As suggested by PSO, there is no default client ID.
// Assumes the specified Client State key has been created.
HRESULT SetAppBranding(const CString& client_state_key_path,
                       const CString& brand_code,
                       const CString& client_id,
                       const CString& referral_id) {
  CORE_LOG(L3, (_T("[app_registry_utils::SetAppBranding][%s][%s][%s][%s]"),
                client_state_key_path, brand_code, client_id, referral_id));

  if (brand_code.GetLength() > kBrandIdLength) {
    return E_INVALIDARG;
  }

  RegKey state_key;
  HRESULT hr = state_key.Create(client_state_key_path);
  if (FAILED(hr)) {
    return hr;
  }

  CString existing_brand_code;
  hr = state_key.GetValue(kRegValueBrandCode, &existing_brand_code);
  if (!existing_brand_code.IsEmpty()) {
    ASSERT1(SUCCEEDED(hr));
    if (existing_brand_code.GetLength() > kBrandIdLength) {
      // Bug 1358852: Brand code garbled with one click.
      VERIFY1(SUCCEEDED(state_key.SetValue(kRegValueBrandCode,
                            existing_brand_code.Left(kBrandIdLength))));
    }
    return S_OK;
  }

  const TCHAR* brand_code_to_write = brand_code.IsEmpty() ?
                                     kDefaultGoogleUpdateBrandCode :
                                     brand_code;
  hr = state_key.SetValue(kRegValueBrandCode, brand_code_to_write);
  if (FAILED(hr)) {
    return hr;
  }

  if (!client_id.IsEmpty()) {
    hr = state_key.SetValue(kRegValueClientId, client_id);
    if (FAILED(hr)) {
      return hr;
    }
  }

  if (!referral_id.IsEmpty()) {
    hr = state_key.SetValue(kRegValueReferralId, referral_id);
    if (FAILED(hr)) {
      return hr;
    }
  }

  const DWORD now = Time64ToInt32(GetCurrent100NSTime());
  VERIFY1(SUCCEEDED(state_key.SetValue(kRegValueInstallTimeSec, now)));

  return S_OK;
}

void PersistSuccessfulInstall(const CString& client_state_key_path,
                              bool is_update,
                              bool is_offline) {
  CORE_LOG(L3,
           (_T("[app_registry_utils::PersistSuccessfulInstall][%s][%d][%d]"),
            client_state_key_path, is_update, is_offline));
  ASSERT1(!is_update || !is_offline);

  ClearUpdateAvailableStats(client_state_key_path);

  if (!is_offline) {
    // TODO(omaha): the semantics of this function are confusing in the
    // case of recording a successful update check. Omaha knows
    // precisely when an update check with the server is being made and it
    // can call PersistSuccessfulUpdateCheck without making any other
    // assumptions.
    //
    // Assumes that all updates are online.
    PersistSuccessfulUpdateCheck(client_state_key_path);
  }

  if (is_update) {
    const DWORD now = Time64ToInt32(GetCurrent100NSTime());
    VERIFY1(SUCCEEDED(RegKey::SetValue(client_state_key_path,
                                       kRegValueLastUpdateTimeSec,
                                       now)));
  }
}

void PersistSuccessfulUpdateCheck(const CString& client_state_key_path) {
  CORE_LOG(L3, (_T("[app_registry_utils::PersistSuccessfulUpdateCheck][%s]"),
                client_state_key_path));
  const DWORD now = Time64ToInt32(GetCurrent100NSTime());
  VERIFY1(SUCCEEDED(RegKey::SetValue(client_state_key_path,
                                     kRegValueLastSuccessfulCheckSec,
                                     now)));
}

void ClearUpdateAvailableStats(const CString& client_state_key_path) {
  CORE_LOG(L3, (_T("[app_registry_utils::ClearUpdateAvailableStats][%s]"),
                client_state_key_path));

  RegKey state_key;
  HRESULT hr = state_key.Open(client_state_key_path);
  if (FAILED(hr)) {
    return;
  }

  VERIFY1(SUCCEEDED(state_key.DeleteValue(kRegValueUpdateAvailableCount)));
  VERIFY1(SUCCEEDED(state_key.DeleteValue(kRegValueUpdateAvailableSince)));
}

HRESULT GetNumClients(bool is_machine, size_t* num_clients) {
  ASSERT1(num_clients);
  RegKey reg_key;
  HKEY root_key = is_machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
  HRESULT hr = reg_key.Open(root_key, GOOPDATE_REG_RELATIVE_CLIENTS, KEY_READ);
  if (FAILED(hr)) {
    return hr;
  }
  DWORD num_subkeys(0);
  LONG error = ::RegQueryInfoKey(reg_key.Key(), NULL, NULL, NULL, &num_subkeys,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL);
  if (error != ERROR_SUCCESS) {
    return HRESULT_FROM_WIN32(error);
  }
  *num_clients = num_subkeys;
  return S_OK;
}

// Reads pv value from Clients key.
void GetAppVersion(bool is_machine, const CString& app_id, CString* pv) {
  ASSERT1(pv);
  RegKey key;
  CString key_name = GetAppClientsKey(is_machine, app_id);
  HRESULT hr = key.Open(key_name, KEY_READ);
  if (SUCCEEDED(hr)) {
    key.GetValue(kRegValueProductVersion, pv);
  }
}

// Reads the following values from the registry:
//  ClientState key
//    pv
//    ap
//    lang
//    brand
//    client
//    iid
//    experiment
void GetClientStateData(bool is_machine,
                        const CString& app_id,
                        CString* pv,
                        CString* ap,
                        CString* lang,
                        CString* brand_code,
                        CString* client_id,
                        CString* iid,
                        CString* experiment_labels) {
  RegKey key;

  CString key_name = GetAppClientStateKey(is_machine, app_id);
  HRESULT hr = key.Open(key_name, KEY_READ);
  if (FAILED(hr)) {
    return;
  }

  if (pv) {
    key.GetValue(kRegValueProductVersion, pv);
  }
  if (ap) {
    key.GetValue(kRegValueAdditionalParams, ap);
  }
  if (lang) {
    key.GetValue(kRegValueLanguage, lang);
  }
  if (brand_code) {
    key.GetValue(kRegValueBrandCode, brand_code);
  }
  if (client_id) {
    key.GetValue(kRegValueClientId, client_id);
  }
  if (iid) {
    key.GetValue(kRegValueInstallationId, iid);
  }
  if (experiment_labels) {
    key.GetValue(kRegValueExperimentLabels, experiment_labels);
  }
}

HRESULT GetUninstalledApps(bool is_machine,
                           std::vector<CString>* app_ids) {
  ASSERT1(app_ids);

  RegKey client_state_key;
  HRESULT hr = client_state_key.Open(
      ConfigManager::Instance()->registry_client_state(is_machine),
      KEY_READ);
  if (FAILED(hr)) {
    return hr;
  }

  int num_sub_keys = client_state_key.GetSubkeyCount();
  for (int i = 0; i < num_sub_keys; ++i) {
    CString app_id;
    hr = client_state_key.GetSubkeyNameAt(i, &app_id);
    if (FAILED(hr)) {
      continue;
    }

    // If the app is not registered, treat it as uninstalled.
    if (!RegKey::HasValue(
        app_registry_utils::GetAppClientsKey(is_machine, app_id),
        kRegValueProductVersion)) {
      app_ids->push_back(app_id);
    }
  }

  return S_OK;
}

HRESULT RemoveClientState(bool is_machine, const CString& app_guid) {
  const CString state_key = GetAppClientStateKey(is_machine, app_guid);
  HRESULT state_hr = RegKey::DeleteKey(state_key, true);
  if (!is_machine) {
    return state_hr;
  }

  const CString state_medium_key = GetAppClientStateMediumKey(is_machine,
                                                              app_guid);
  HRESULT state_medium_hr = RegKey::DeleteKey(state_medium_key, true);
  return FAILED(state_hr) ? state_hr : state_medium_hr;
}

void RemoveClientStateForApps(bool is_machine,
                              const std::vector<CString>& apps) {
  std::vector<CString>::const_iterator it;
  for (it = apps.begin(); it != apps.end(); ++it) {
    RemoveClientState(is_machine, *it);
  }
}

HRESULT GetExperimentLabels(bool is_machine, const CString& app_id,
                            CString* labels_out) {
  ASSERT1(!app_id.IsEmpty());
  ASSERT1(labels_out);

  const CString state_key = GetAppClientStateKey(is_machine, app_id);
  if (!RegKey::HasValue(state_key, kRegValueExperimentLabels)) {
    return S_OK;
  }

  return RegKey::GetValue(state_key, kRegValueExperimentLabels, labels_out);
}

HRESULT SetExperimentLabels(bool is_machine, const CString& app_id,
                            const CString& new_labels) {
  ASSERT1(!app_id.IsEmpty());
  ASSERT1(ExperimentLabels::IsStringValidLabelSet(new_labels));

  return RegKey::SetValue(GetAppClientStateKey(is_machine, app_id),
                          kRegValueExperimentLabels,
                          new_labels);
}

}  // namespace app_registry_utils

}  // namespace omaha
