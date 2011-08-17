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

#include <limits.h>
#include "omaha/base/const_addresses.h"
#include "omaha/base/constants.h"
#include "omaha/base/file.h"
#include "omaha/base/reg_key.h"
#include "omaha/base/string.h"
#include "omaha/base/system_info.h"
#include "omaha/base/time.h"
#include "omaha/base/utils.h"
#include "omaha/base/vistautil.h"
#include "omaha/common/config_manager.h"
#include "omaha/common/const_goopdate.h"
#include "omaha/testing/unit_test.h"

namespace omaha {

namespace {

// OMAHA_KEY_REL == "Software\Google\Update"
#define OMAHA_KEY_REL \
    _T("Software\\") SHORT_COMPANY_NAME _T("\\") PRODUCT_NAME


#define APP_GUID1 _T("{6762F466-8863-424f-817C-5757931F346E}")
const TCHAR* const kAppGuid1 = APP_GUID1;
const TCHAR* const kAppMachineClientStatePath1 =
    _T("HKLM\\") OMAHA_KEY_REL _T("\\ClientState\\") APP_GUID1;
const TCHAR* const kAppUserClientStatePath1 =
    _T("HKCU\\") OMAHA_KEY_REL _T("\\ClientState\\") APP_GUID1;
const TCHAR* const kAppMachineClientStateMediumPath1 =
    _T("HKLM\\") OMAHA_KEY_REL _T("\\ClientStateMedium\\") APP_GUID1;

#define APP_GUID2 _T("{8A0FDD16-D4B7-4167-893F-1386F2A2F0FB}")
const TCHAR* const kAppGuid2 = APP_GUID2;
const TCHAR* const kAppMachineClientStatePath2 =
    _T("HKLM\\") OMAHA_KEY_REL _T("\\ClientState\\") APP_GUID2;
const TCHAR* const kAppUserClientStatePath2 =
    _T("HKCU\\") OMAHA_KEY_REL _T("\\ClientState\\") APP_GUID2;

const TCHAR* const kPolicyKey =
    _T("HKLM\\Software\\Policies\\")
    SHORT_COMPANY_NAME _T("\\") PRODUCT_NAME _T("\\");
const TCHAR* const kInstallPolicyApp1 = _T("Install") APP_GUID1;
const TCHAR* const kInstallPolicyApp2 = _T("Install") APP_GUID2;
const TCHAR* const kUpdatePolicyApp1 = _T("Update") APP_GUID1;
const TCHAR* const kUpdatePolicyApp2 = _T("Update") APP_GUID2;

// Helper to write policies to the registry. Eliminates ambiguity of which
// overload of SetValue to use without the need for static_cast.
HRESULT SetPolicy(const TCHAR* policy_name, DWORD value) {
  return RegKey::SetValue(kPolicyKey, policy_name, value);
}

// DeleteDirectory can fail with ERROR_PATH_NOT_FOUND if the parent directory
// does not exist. Consider this a success for testing purposes.
HRESULT DeleteTestDirectory(const TCHAR* dir) {
  HRESULT hr = DeleteDirectory(dir);
  if (hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) {
    return S_OK;
  }
  return hr;
}

}  // namespace

class ConfigManagerNoOverrideTest : public testing::Test {
 protected:
  ConfigManagerNoOverrideTest()
      : cm_(ConfigManager::Instance()) {
  }

  bool CanInstallApp(const TCHAR* guid) {
    return cm_->CanInstallApp(StringToGuid(guid));
  }

  bool CanUpdateApp(const TCHAR* guid, bool is_manual) {
    return cm_->CanUpdateApp(StringToGuid(guid), is_manual);
  }

  ConfigManager* cm_;
};

class ConfigManagerTest : public ConfigManagerNoOverrideTest {
 protected:
  ConfigManagerTest()
      : hive_override_key_name_(kRegistryHiveOverrideRoot) {
  }

  virtual void SetUp() {
    RegKey::DeleteKey(hive_override_key_name_, true);
    OverrideRegistryHives(hive_override_key_name_);
  }

  virtual void TearDown() {
    RestoreRegistryHives();
    EXPECT_SUCCEEDED(RegKey::DeleteKey(hive_override_key_name_, true));
  }

  void CanCollectStatsHelper(bool is_machine);
  void CanCollectStatsIgnoresOppositeHiveHelper(bool is_machine);
  HRESULT SetFirstInstallTime(bool is_machine, DWORD time);
  HRESULT DeleteFirstInstallTime(bool is_machine);
  HRESULT SetUpdateTime(bool is_machine, DWORD time);
  HRESULT DeleteUpdateTime(bool is_machine);

  CString hive_override_key_name_;
};

void ConfigManagerTest::CanCollectStatsHelper(bool is_machine) {
  const TCHAR* app1_state_key_name = is_machine ? kAppMachineClientStatePath1 :
                                                  kAppUserClientStatePath1;

  EXPECT_FALSE(cm_->CanCollectStats(is_machine));

  // Test the 'UsageStats' override.
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueForceUsageStats,
                                    _T("")));
  EXPECT_TRUE(cm_->CanCollectStats(is_machine));
  EXPECT_SUCCEEDED(RegKey::DeleteValue(MACHINE_REG_UPDATE_DEV,
                                       kRegValueForceUsageStats));

  DWORD val = 1;
  EXPECT_SUCCEEDED(RegKey::SetValue(app1_state_key_name,
                                    _T("usagestats"),
                                    val));
  EXPECT_TRUE(cm_->CanCollectStats(is_machine));

  val = 2;  // invalid value
  EXPECT_SUCCEEDED(RegKey::SetValue(app1_state_key_name,
                                    _T("usagestats"),
                                    val));
  EXPECT_FALSE(cm_->CanCollectStats(is_machine));

  val = 0;
  EXPECT_SUCCEEDED(RegKey::SetValue(app1_state_key_name,
                                    _T("usagestats"),
                                    val));
  EXPECT_FALSE(cm_->CanCollectStats(is_machine));

  // One 0 and one 1 results in true. The alphabetical order of the GUIDs is
  // important assuming GetSubkeyNameAt returns subkeys in alphabetical order.
  const TCHAR* app2_state_key_name = is_machine ? kAppMachineClientStatePath2 :
                                                  kAppUserClientStatePath2;
  val = 1;
  EXPECT_SUCCEEDED(RegKey::SetValue(app2_state_key_name,
                                    _T("usagestats"),
                                    val));
  EXPECT_TRUE(cm_->CanCollectStats(is_machine));
}

void ConfigManagerTest::CanCollectStatsIgnoresOppositeHiveHelper(
    bool is_machine) {
  const TCHAR* app1_state_key_name = is_machine ? kAppMachineClientStatePath1 :
                                                  kAppUserClientStatePath1;

  EXPECT_FALSE(cm_->CanCollectStats(is_machine));

  DWORD val = 1;
  EXPECT_SUCCEEDED(RegKey::SetValue(app1_state_key_name,
                                    _T("usagestats"),
                                    val));
  EXPECT_TRUE(cm_->CanCollectStats(is_machine));
  EXPECT_FALSE(cm_->CanCollectStats(!is_machine));
}

HRESULT ConfigManagerTest::SetFirstInstallTime(bool is_machine, DWORD time) {
  return RegKey::SetValue(cm_->registry_client_state_goopdate(is_machine),
                          kRegValueInstallTimeSec,
                          time);
}

HRESULT ConfigManagerTest::DeleteFirstInstallTime(bool is_machine) {
  if (!RegKey::HasValue(cm_->registry_client_state_goopdate(is_machine),
                        kRegValueInstallTimeSec)) {
    return S_OK;
  }

  return RegKey::DeleteValue(cm_->registry_client_state_goopdate(is_machine),
                             kRegValueInstallTimeSec);
}

HRESULT ConfigManagerTest::SetUpdateTime(bool is_machine, DWORD time) {
  return RegKey::SetValue(cm_->registry_client_state_goopdate(is_machine),
                          kRegValueLastUpdateTimeSec,
                          time);
}

HRESULT ConfigManagerTest::DeleteUpdateTime(bool is_machine) {
  if (!RegKey::HasValue(cm_->registry_client_state_goopdate(is_machine),
                        kRegValueLastUpdateTimeSec)) {
    return S_OK;
  }

  return RegKey::DeleteValue(cm_->registry_client_state_goopdate(is_machine),
                             kRegValueLastUpdateTimeSec);
}

TEST_F(ConfigManagerNoOverrideTest, RegistryKeys) {
  EXPECT_STREQ(_T("HKCU\\") OMAHA_KEY_REL _T("\\Clients\\"),
               cm_->user_registry_clients());
  EXPECT_STREQ(_T("HKLM\\") OMAHA_KEY_REL _T("\\Clients\\"),
               cm_->machine_registry_clients());
  EXPECT_STREQ(_T("HKCU\\") OMAHA_KEY_REL _T("\\Clients\\"),
               cm_->registry_clients(false));
  EXPECT_STREQ(_T("HKLM\\") OMAHA_KEY_REL _T("\\Clients\\"),
               cm_->registry_clients(true));

  EXPECT_STREQ(_T("HKCU\\") OMAHA_KEY_REL _T("\\Clients\\") GOOPDATE_APP_ID,
               cm_->user_registry_clients_goopdate());
  EXPECT_STREQ(_T("HKLM\\") OMAHA_KEY_REL _T("\\Clients\\") GOOPDATE_APP_ID,
               cm_->machine_registry_clients_goopdate());
  EXPECT_STREQ(_T("HKCU\\") OMAHA_KEY_REL _T("\\Clients\\") GOOPDATE_APP_ID,
               cm_->registry_clients_goopdate(false));
  EXPECT_STREQ(_T("HKLM\\") OMAHA_KEY_REL _T("\\Clients\\") GOOPDATE_APP_ID,
               cm_->registry_clients_goopdate(true));

  EXPECT_STREQ(_T("HKCU\\") OMAHA_KEY_REL _T("\\ClientState\\"),
               cm_->user_registry_client_state());
  EXPECT_STREQ(_T("HKLM\\") OMAHA_KEY_REL _T("\\ClientState\\"),
               cm_->machine_registry_client_state());
  EXPECT_STREQ(_T("HKCU\\") OMAHA_KEY_REL _T("\\ClientState\\"),
               cm_->registry_client_state(false));
  EXPECT_STREQ(_T("HKLM\\") OMAHA_KEY_REL _T("\\ClientState\\"),
               cm_->registry_client_state(true));

  EXPECT_STREQ(_T("HKCU\\") OMAHA_KEY_REL _T("\\ClientState\\") GOOPDATE_APP_ID,
               cm_->user_registry_client_state_goopdate());
  EXPECT_STREQ(_T("HKLM\\") OMAHA_KEY_REL _T("\\ClientState\\") GOOPDATE_APP_ID,
               cm_->machine_registry_client_state_goopdate());
  EXPECT_STREQ(_T("HKCU\\") OMAHA_KEY_REL _T("\\ClientState\\") GOOPDATE_APP_ID,
               cm_->registry_client_state_goopdate(false));
  EXPECT_STREQ(_T("HKLM\\") OMAHA_KEY_REL _T("\\ClientState\\") GOOPDATE_APP_ID,
               cm_->registry_client_state_goopdate(true));

  EXPECT_STREQ(_T("HKLM\\") OMAHA_KEY_REL _T("\\ClientStateMedium\\"),
               cm_->machine_registry_client_state_medium());

  EXPECT_STREQ(_T("HKCU\\") OMAHA_KEY_REL _T("\\"),
               cm_->user_registry_update());
  EXPECT_STREQ(_T("HKLM\\") OMAHA_KEY_REL _T("\\"),
               cm_->machine_registry_update());
  EXPECT_STREQ(_T("HKCU\\") OMAHA_KEY_REL _T("\\"),
               cm_->registry_update(false));
  EXPECT_STREQ(_T("HKLM\\") OMAHA_KEY_REL _T("\\"),
               cm_->registry_update(true));

  EXPECT_STREQ(_T("HKCU\\Software\\") COMPANY_NAME_IDENTIFIER _T("\\"),
               cm_->user_registry_google());
  EXPECT_STREQ(_T("HKLM\\Software\\") COMPANY_NAME_IDENTIFIER _T("\\"),
               cm_->machine_registry_google());
  EXPECT_STREQ(_T("HKCU\\Software\\") COMPANY_NAME_IDENTIFIER _T("\\"),
               cm_->registry_google(false));
  EXPECT_STREQ(_T("HKLM\\Software\\") COMPANY_NAME_IDENTIFIER _T("\\"),
               cm_->registry_google(true));
}

TEST_F(ConfigManagerNoOverrideTest, GetUserCrashReportsDir) {
  const CString expected_path = GetGoogleUserPath() + _T("CrashReports");
  EXPECT_SUCCEEDED(DeleteTestDirectory(expected_path));
  EXPECT_STREQ(expected_path, cm_->GetUserCrashReportsDir());
  EXPECT_TRUE(File::Exists(expected_path));
}

// Should run before the subdirectory tests to ensure the directory is created.
TEST_F(ConfigManagerNoOverrideTest, GetUserGoopdateInstallDir) {
  const CString expected_path = GetGoogleUserPath() + _T("Update");
  EXPECT_STREQ(expected_path, cm_->GetUserGoopdateInstallDir());
  EXPECT_TRUE(File::Exists(expected_path));
}

TEST_F(ConfigManagerNoOverrideTest, GetUserDownloadStorageDir) {
  const CString expected_path = GetGoogleUpdateUserPath() + _T("Download");
  EXPECT_SUCCEEDED(DeleteTestDirectory(expected_path));
  EXPECT_STREQ(expected_path, cm_->GetUserDownloadStorageDir());
  EXPECT_TRUE(File::Exists(expected_path));
}

TEST_F(ConfigManagerNoOverrideTest, GetUserInstallWorkingDir) {
  const CString expected_path = GetGoogleUpdateUserPath() + _T("Install");
  EXPECT_SUCCEEDED(DeleteTestDirectory(expected_path));
  EXPECT_STREQ(expected_path, cm_->GetUserInstallWorkingDir());
  EXPECT_TRUE(File::Exists(expected_path));
}

TEST_F(ConfigManagerNoOverrideTest, GetUserOfflineStorageDir) {
  const CString expected_path = GetGoogleUpdateUserPath() + _T("Offline");
  EXPECT_SUCCEEDED(DeleteTestDirectory(expected_path));
  EXPECT_STREQ(expected_path, cm_->GetUserOfflineStorageDir());
  EXPECT_TRUE(File::Exists(expected_path));
}

TEST_F(ConfigManagerNoOverrideTest, IsRunningFromUserGoopdateInstallDir) {
  EXPECT_FALSE(cm_->IsRunningFromUserGoopdateInstallDir());
}

TEST_F(ConfigManagerNoOverrideTest, GetTempDownloadDir) {
  TCHAR expected_path[MAX_PATH] = {0};
  ASSERT_NE(0, ::GetTempPath(MAX_PATH, expected_path));

  EXPECT_STREQ(expected_path, cm_->GetTempDownloadDir());
  EXPECT_TRUE(File::Exists(expected_path));
}

TEST_F(ConfigManagerNoOverrideTest, GetMachineCrashReportsDir) {
  CString program_files;
  EXPECT_SUCCEEDED(GetFolderPath(CSIDL_PROGRAM_FILES, &program_files));
  const CString expected_path =
      program_files + _T("\\") + SHORT_COMPANY_NAME + _T("\\CrashReports");
  EXPECT_SUCCEEDED(DeleteTestDirectory(expected_path));
  EXPECT_STREQ(expected_path, cm_->GetMachineCrashReportsDir());
  EXPECT_TRUE(File::Exists(expected_path) || !vista_util::IsUserAdmin());
}

// Should run before the subdirectory tests to ensure the directory is created.
TEST_F(ConfigManagerNoOverrideTest, GetMachineGoopdateInstallDir) {
  CString expected_path = GetGoogleUpdateMachinePath();
  EXPECT_STREQ(expected_path, cm_->GetMachineGoopdateInstallDir());
  EXPECT_TRUE(File::Exists(expected_path) || !vista_util::IsUserAdmin());
}

TEST_F(ConfigManagerNoOverrideTest, GetMachineSecureDownloadStorageDir) {
  CString expected_path = GetGoogleUpdateMachinePath() + _T("\\Download");
  EXPECT_SUCCEEDED(DeleteTestDirectory(expected_path));
  EXPECT_STREQ(expected_path, cm_->GetMachineSecureDownloadStorageDir());
  EXPECT_TRUE(File::Exists(expected_path) || !vista_util::IsUserAdmin());
}

TEST_F(ConfigManagerNoOverrideTest, GetMachineInstallWorkingDir) {
  CString expected_path = GetGoogleUpdateMachinePath() + _T("\\Install");
  EXPECT_SUCCEEDED(DeleteTestDirectory(expected_path));
  EXPECT_STREQ(expected_path, cm_->GetMachineInstallWorkingDir());
  EXPECT_TRUE(File::Exists(expected_path) || !vista_util::IsUserAdmin());
}

TEST_F(ConfigManagerNoOverrideTest, GetMachineSecureOfflineStorageDir) {
  CString expected_path = GetGoogleUpdateMachinePath() + _T("\\Offline");
  EXPECT_SUCCEEDED(DeleteTestDirectory(expected_path));
  EXPECT_STREQ(expected_path, cm_->GetMachineSecureOfflineStorageDir());
  EXPECT_TRUE(File::Exists(expected_path) || !vista_util::IsUserAdmin());
}

TEST_F(ConfigManagerNoOverrideTest, IsRunningFromMachineGoopdateInstallDir) {
  EXPECT_FALSE(cm_->IsRunningFromMachineGoopdateInstallDir());
}

// Tests the GetUpdateCheckUrl override.
TEST_F(ConfigManagerTest, GetUpdateCheckUrl) {
  CString url;
  EXPECT_SUCCEEDED(cm_->GetUpdateCheckUrl(&url));
  EXPECT_STREQ(url, kUrlUpdateCheck);

  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueNameUrl,
                                    _T("http://updatecheck/")));
  url.Empty();
  EXPECT_SUCCEEDED(cm_->GetUpdateCheckUrl(&url));
  EXPECT_STREQ(url, _T("http://updatecheck/"));
}

// Tests the GetPingUrl override.
TEST_F(ConfigManagerTest, GetPingUrl) {
  CString url;
  EXPECT_SUCCEEDED(cm_->GetPingUrl(&url));
  EXPECT_STREQ(url, kUrlPing);

  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueNamePingUrl,
                                    _T("http://ping/")));
  url.Empty();
  EXPECT_SUCCEEDED(cm_->GetPingUrl(&url));
  EXPECT_STREQ(url, _T("http://ping/"));
}

// Tests the GetCrashReportUrl override.
TEST_F(ConfigManagerTest, GetCrashReportUrl) {
  CString url;
  EXPECT_SUCCEEDED(cm_->GetCrashReportUrl(&url));
  EXPECT_STREQ(url, kUrlCrashReport);

  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueNameCrashReportUrl,
                                    _T("http://crashreport/")));
  url.Empty();
  EXPECT_SUCCEEDED(cm_->GetCrashReportUrl(&url));
  EXPECT_STREQ(url, _T("http://crashreport/"));
}

// Tests the GetMoreInfoUrl override.
TEST_F(ConfigManagerTest, GetMoreInfoUrl) {
  CString url;
  EXPECT_SUCCEEDED(cm_->GetMoreInfoUrl(&url));
  EXPECT_STREQ(url, kUrlMoreInfo);

  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueNameGetMoreInfoUrl,
                                    _T("http://moreinfo/")));
  url.Empty();
  EXPECT_SUCCEEDED(cm_->GetMoreInfoUrl(&url));
  EXPECT_STREQ(url, _T("http://moreinfo/"));
}

// Tests the GetUsageStatsReportUrl override.
TEST_F(ConfigManagerTest, GetUsageStatsReportUrl) {
  CString url;
  EXPECT_SUCCEEDED(cm_->GetUsageStatsReportUrl(&url));
  EXPECT_STREQ(url, kUrlUsageStatsReport);

  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueNameUsageStatsReportUrl,
                                    _T("http://usagestatsreport/")));
  url.Empty();
  EXPECT_SUCCEEDED(cm_->GetUsageStatsReportUrl(&url));
  EXPECT_STREQ(url, _T("http://usagestatsreport/"));
}

// Tests LastCheckPeriodSec override.
TEST_F(ConfigManagerTest, GetLastCheckPeriodSec_Default) {
  bool is_overridden = true;
  if (cm_->IsInternalUser()) {
    EXPECT_EQ(kLastCheckPeriodInternalUserSec,
              cm_->GetLastCheckPeriodSec(&is_overridden));
  } else {
    EXPECT_EQ(kLastCheckPeriodSec, cm_->GetLastCheckPeriodSec(&is_overridden));
  }
  EXPECT_FALSE(is_overridden);
}

TEST_F(ConfigManagerTest, GetLastCheckPeriodSec_UpdateDevOverride) {
  // Zero is a special value meaning disabled.
  DWORD val = 0;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueLastCheckPeriodSec,
                                    val));
  bool is_overridden = false;
  EXPECT_EQ(0, cm_->GetLastCheckPeriodSec(&is_overridden));
  EXPECT_TRUE(is_overridden);

  val = kMinLastCheckPeriodSec - 1;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueLastCheckPeriodSec,
                                    val));
  is_overridden = false;
  EXPECT_EQ(kMinLastCheckPeriodSec, cm_->GetLastCheckPeriodSec(&is_overridden));
  EXPECT_TRUE(is_overridden);

  val = INT_MAX + static_cast<uint32>(1);
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueLastCheckPeriodSec,
                                    val));
  is_overridden = false;
  EXPECT_EQ(INT_MAX, cm_->GetLastCheckPeriodSec(&is_overridden));
  EXPECT_TRUE(is_overridden);

  val = 1000;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueLastCheckPeriodSec,
                                    val));
  is_overridden = false;
  EXPECT_EQ(1000, cm_->GetLastCheckPeriodSec(&is_overridden));
  EXPECT_TRUE(is_overridden);

  EXPECT_SUCCEEDED(RegKey::DeleteValue(MACHINE_REG_UPDATE_DEV,
                                       kRegValueLastCheckPeriodSec));
  is_overridden = true;
  if (cm_->IsInternalUser()) {
    EXPECT_EQ(kLastCheckPeriodInternalUserSec,
              cm_->GetLastCheckPeriodSec(&is_overridden));
  } else {
    EXPECT_EQ(kLastCheckPeriodSec, cm_->GetLastCheckPeriodSec(&is_overridden));
  }
  EXPECT_FALSE(is_overridden);
}

TEST_F(ConfigManagerTest, GetLastCheckPeriodSec_GroupPolicyOverride) {
  const DWORD kOverrideMinutes = 16000;
  const DWORD kExpectedSeconds = kOverrideMinutes * 60;
  EXPECT_SUCCEEDED(SetPolicy(_T("AutoUpdateCheckPeriodMinutes"),
                             kOverrideMinutes));
  bool is_overridden = false;
  EXPECT_EQ(kExpectedSeconds, cm_->GetLastCheckPeriodSec(&is_overridden));
  EXPECT_TRUE(is_overridden);
}

TEST_F(ConfigManagerTest, GetLastCheckPeriodSec_GroupPolicyOverride_TooLow) {
  const DWORD kOverrideMinutes = 1;
  EXPECT_SUCCEEDED(SetPolicy(_T("AutoUpdateCheckPeriodMinutes"),
                             kOverrideMinutes));
  bool is_overridden = false;
  EXPECT_EQ(kMinLastCheckPeriodSec, cm_->GetLastCheckPeriodSec(&is_overridden));
  EXPECT_TRUE(is_overridden);
}

TEST_F(ConfigManagerTest, GetLastCheckPeriodSec_GroupPolicyOverride_Zero) {
  const DWORD kOverrideMinutes = 0;
  EXPECT_SUCCEEDED(SetPolicy(_T("AutoUpdateCheckPeriodMinutes"),
                             kOverrideMinutes));
  bool is_overridden = false;
  EXPECT_EQ(0, cm_->GetLastCheckPeriodSec(&is_overridden));
  EXPECT_TRUE(is_overridden);
}

TEST_F(ConfigManagerTest,
       GetLastCheckPeriodSec_GroupPolicyOverride_Overflow_SecondsConversion) {
  const DWORD kOverrideMinutes = UINT_MAX;
  EXPECT_SUCCEEDED(SetPolicy(_T("AutoUpdateCheckPeriodMinutes"),
                             kOverrideMinutes));
  bool is_overridden = false;
  EXPECT_EQ(INT_MAX, cm_->GetLastCheckPeriodSec(&is_overridden));
  EXPECT_TRUE(is_overridden);

  const DWORD kOverrideMinutes2 = INT_MAX + static_cast<uint32>(1);
  EXPECT_SUCCEEDED(SetPolicy(_T("AutoUpdateCheckPeriodMinutes"),
                             kOverrideMinutes2));
  is_overridden = false;
  EXPECT_EQ(INT_MAX, cm_->GetLastCheckPeriodSec(&is_overridden));
  EXPECT_TRUE(is_overridden);

  const DWORD kOverrideMinutes3 = 0xf0000000;
  EXPECT_SUCCEEDED(SetPolicy(_T("AutoUpdateCheckPeriodMinutes"),
                             kOverrideMinutes3));
  is_overridden = false;
  EXPECT_EQ(INT_MAX, cm_->GetLastCheckPeriodSec(&is_overridden));
  EXPECT_TRUE(is_overridden);
}

// Overflow the integer but not the minutes to seconds conversion.
TEST_F(ConfigManagerTest,
       GetLastCheckPeriodSec_GroupPolicyOverride_Overflow_Int) {
  const DWORD kOverrideMinutes = UINT_MAX / 60;
  EXPECT_GT(UINT_MAX, kOverrideMinutes);

  EXPECT_SUCCEEDED(SetPolicy(_T("AutoUpdateCheckPeriodMinutes"),
                             kOverrideMinutes));
  bool is_overridden = false;
  EXPECT_EQ(INT_MAX, cm_->GetLastCheckPeriodSec(&is_overridden));
  EXPECT_TRUE(is_overridden);
}

// UpdateDev takes precedence over the Group Policy override.
TEST_F(ConfigManagerTest,
       GetLastCheckPeriodSec_GroupPolicyAndUpdateDevOverrides) {
  const DWORD kGroupPolicyOverrideMinutes = 100;
  EXPECT_SUCCEEDED(SetPolicy(_T("AutoUpdateCheckPeriodMinutes"),
                             kGroupPolicyOverrideMinutes));
  const DWORD kUpdateDevOverrideSeconds = 70;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueLastCheckPeriodSec,
                                    kUpdateDevOverrideSeconds));

  bool is_overridden = false;
  EXPECT_EQ(kUpdateDevOverrideSeconds,
            cm_->GetLastCheckPeriodSec(&is_overridden));
  EXPECT_TRUE(is_overridden);
}

TEST_F(ConfigManagerTest, CanCollectStats_LegacyLocationNewName) {
  DWORD val = 1;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE,
                                    _T("usagestats"),
                                    val));
  EXPECT_FALSE(cm_->CanCollectStats(true));
}

TEST_F(ConfigManagerTest, CanCollectStats_MachineOnly) {
  CanCollectStatsHelper(true);
}

TEST_F(ConfigManagerTest, CanCollectStats_UserOnly) {
  CanCollectStatsHelper(false);
}

// This tests that the legacy conversion is honored.
TEST_F(ConfigManagerTest, CanCollectStats_GoopdateGuidIsChecked) {
  EXPECT_FALSE(cm_->CanCollectStats(true));

  DWORD val = 1;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_CLIENT_STATE_GOOPDATE,
                                    _T("usagestats"),
                                    val));
  EXPECT_TRUE(cm_->CanCollectStats(true));
}

TEST_F(ConfigManagerTest, CanCollectStats_MachineIgnoresUser) {
  CanCollectStatsIgnoresOppositeHiveHelper(true);
}

TEST_F(ConfigManagerTest, CanCollectStats_UserIgnoresMachine) {
  CanCollectStatsIgnoresOppositeHiveHelper(false);
}
// Unfortunately, the app's ClientStateMedium key is not checked if there is no
// corresponding ClientState key.
TEST_F(ConfigManagerTest,
       CanCollectStats_Machine_ClientStateMediumOnly_AppClientStateKeyMissing) {
  EXPECT_SUCCEEDED(RegKey::SetValue(kAppMachineClientStateMediumPath1,
                                    _T("usagestats"),
                                    static_cast<DWORD>(1)));
  EXPECT_FALSE(cm_->CanCollectStats(true));
}

TEST_F(ConfigManagerTest,
       CanCollectStats_Machine_ClientStateMediumOnly_AppClientStateKeyExists) {
  EXPECT_SUCCEEDED(RegKey::CreateKey(kAppMachineClientStatePath1));
  EXPECT_SUCCEEDED(RegKey::SetValue(kAppMachineClientStateMediumPath1,
                                    _T("usagestats"),
                                    static_cast<DWORD>(1)));
  EXPECT_TRUE(cm_->CanCollectStats(true));
}

TEST_F(ConfigManagerTest,
       CanCollectStats_Machine_ClientStateMediumInvalid) {
  EXPECT_SUCCEEDED(RegKey::CreateKey(kAppMachineClientStatePath1));
  EXPECT_SUCCEEDED(RegKey::SetValue(kAppMachineClientStateMediumPath1,
                                    _T("usagestats"),
                                    static_cast<DWORD>(2)));
  EXPECT_FALSE(cm_->CanCollectStats(true));
}

TEST_F(ConfigManagerTest, CanCollectStats_User_ClientStateMediumOnly) {
  EXPECT_SUCCEEDED(RegKey::CreateKey(kAppUserClientStatePath1));
  EXPECT_SUCCEEDED(RegKey::SetValue(
      _T("HKCU\\") OMAHA_KEY_REL _T("\\ClientStateMedium\\") APP_GUID1,
      _T("usagestats"),
      static_cast<DWORD>(1)));
  EXPECT_FALSE(cm_->CanCollectStats(false));
}

TEST_F(ConfigManagerTest,
       CanCollectStats_Machine_ClientStateZeroClientStateMediumOne) {
  EXPECT_SUCCEEDED(RegKey::SetValue(kAppMachineClientStatePath1,
                                    _T("usagestats"),
                                    static_cast<DWORD>(0)));
  EXPECT_FALSE(cm_->CanCollectStats(true));

  EXPECT_SUCCEEDED(RegKey::SetValue(kAppMachineClientStateMediumPath1,
                                    _T("usagestats"),
                                    static_cast<DWORD>(1)));
  EXPECT_TRUE(cm_->CanCollectStats(true));
}

TEST_F(ConfigManagerTest,
       CanCollectStats_Machine_ClientStateOneClientStateMediumZero) {
  EXPECT_SUCCEEDED(RegKey::SetValue(kAppMachineClientStatePath1,
                                    _T("usagestats"),
                                    static_cast<DWORD>(1)));
  EXPECT_TRUE(cm_->CanCollectStats(true));

  EXPECT_SUCCEEDED(RegKey::SetValue(kAppMachineClientStateMediumPath1,
                                    _T("usagestats"),
                                    static_cast<DWORD>(0)));
  EXPECT_FALSE(cm_->CanCollectStats(true));
}

// Tests OverInstall override.
TEST_F(ConfigManagerTest, CanOverInstall) {
  EXPECT_EQ(cm_->CanOverInstall(), !OFFICIAL_BUILD);

  DWORD val = 1;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueNameOverInstall,
                                    val));
#ifdef DEBUG
  EXPECT_TRUE(cm_->CanOverInstall());
#else
  EXPECT_EQ(!OFFICIAL_BUILD, cm_->CanOverInstall());
#endif

  val = 0;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueNameOverInstall,
                                    val));
#ifdef DEBUG
  EXPECT_FALSE(cm_->CanOverInstall());
#else
  EXPECT_EQ(!OFFICIAL_BUILD, cm_->CanOverInstall());
#endif
}

// Tests AuCheckPeriodMs override.
TEST_F(ConfigManagerTest, GetAutoUpdateTimerIntervalMs) {
  EXPECT_EQ(cm_->IsInternalUser() ? kAUCheckPeriodInternalUserMs :
                                    kAUCheckPeriodMs,
            cm_->GetAutoUpdateTimerIntervalMs());

  DWORD val = 0;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueAuCheckPeriodMs,
                                    val));
  EXPECT_EQ(kMinAUCheckPeriodMs, cm_->GetAutoUpdateTimerIntervalMs());

  val = kMinAUCheckPeriodMs - 1;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueAuCheckPeriodMs,
                                    val));
  EXPECT_EQ(kMinAUCheckPeriodMs, cm_->GetAutoUpdateTimerIntervalMs());

  val = 30000;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueAuCheckPeriodMs,
                                    val));
  EXPECT_EQ(val, cm_->GetAutoUpdateTimerIntervalMs());

  val = INT_MAX;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueAuCheckPeriodMs,
                                    val));
  EXPECT_EQ(val, cm_->GetAutoUpdateTimerIntervalMs());

  // Tests overflow with large positive numbers.
  val = INT_MAX;
  ++val;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueAuCheckPeriodMs,
                                    val));
  EXPECT_EQ(INT_MAX, cm_->GetAutoUpdateTimerIntervalMs());

  val = UINT_MAX;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueAuCheckPeriodMs,
                                    val));
  EXPECT_EQ(INT_MAX, cm_->GetAutoUpdateTimerIntervalMs());
}

// Tests CrCheckPeriodMs override.
TEST_F(ConfigManagerTest, GetCodeRedTimerIntervalMs) {
  EXPECT_EQ(kCodeRedCheckPeriodMs, cm_->GetCodeRedTimerIntervalMs());

  DWORD val = 0;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueCrCheckPeriodMs,
                                    val));
  EXPECT_EQ(kMinCodeRedCheckPeriodMs, cm_->GetCodeRedTimerIntervalMs());

  val = kMinCodeRedCheckPeriodMs - 1;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueCrCheckPeriodMs,
                                    val));
  EXPECT_EQ(kMinCodeRedCheckPeriodMs, cm_->GetCodeRedTimerIntervalMs());

  val = 60000;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueCrCheckPeriodMs,
                                    val));
  EXPECT_EQ(val, cm_->GetCodeRedTimerIntervalMs());

  val = INT_MAX;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueCrCheckPeriodMs,
                                    val));
  EXPECT_EQ(val, cm_->GetCodeRedTimerIntervalMs());

  // Tests overflow with large positive numbers.
  val = INT_MAX;
  ++val;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueCrCheckPeriodMs,
                                    val));
  EXPECT_EQ(INT_MAX, cm_->GetCodeRedTimerIntervalMs());

  val = UINT_MAX;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                      kRegValueCrCheckPeriodMs,
                                      val));
  EXPECT_EQ(INT_MAX, cm_->GetCodeRedTimerIntervalMs());
}

// Tests CanLogEvents override.
TEST_F(ConfigManagerTest, CanLogEvents_WithOutOverride) {
  EXPECT_FALSE(cm_->CanLogEvents(EVENTLOG_SUCCESS));
  EXPECT_TRUE(cm_->CanLogEvents(EVENTLOG_ERROR_TYPE));
  EXPECT_TRUE(cm_->CanLogEvents(EVENTLOG_WARNING_TYPE));
  EXPECT_FALSE(cm_->CanLogEvents(EVENTLOG_INFORMATION_TYPE));
  EXPECT_FALSE(cm_->CanLogEvents(EVENTLOG_AUDIT_SUCCESS));
  EXPECT_FALSE(cm_->CanLogEvents(EVENTLOG_AUDIT_FAILURE));
}

TEST_F(ConfigManagerTest, CanLogEvents) {
  EXPECT_FALSE(cm_->CanLogEvents(EVENTLOG_INFORMATION_TYPE));

  DWORD val = LOG_EVENT_LEVEL_ALL;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueEventLogLevel,
                                    val));
  EXPECT_TRUE(cm_->CanLogEvents(EVENTLOG_SUCCESS));
  EXPECT_TRUE(cm_->CanLogEvents(EVENTLOG_ERROR_TYPE));
  EXPECT_TRUE(cm_->CanLogEvents(EVENTLOG_WARNING_TYPE));
  EXPECT_TRUE(cm_->CanLogEvents(EVENTLOG_INFORMATION_TYPE));
  EXPECT_TRUE(cm_->CanLogEvents(EVENTLOG_AUDIT_SUCCESS));
  EXPECT_TRUE(cm_->CanLogEvents(EVENTLOG_AUDIT_FAILURE));

  val = LOG_EVENT_LEVEL_WARN_AND_ERROR;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueEventLogLevel,
                                    val));
  EXPECT_FALSE(cm_->CanLogEvents(EVENTLOG_SUCCESS));
  EXPECT_TRUE(cm_->CanLogEvents(EVENTLOG_ERROR_TYPE));
  EXPECT_TRUE(cm_->CanLogEvents(EVENTLOG_WARNING_TYPE));
  EXPECT_FALSE(cm_->CanLogEvents(EVENTLOG_INFORMATION_TYPE));
  EXPECT_FALSE(cm_->CanLogEvents(EVENTLOG_AUDIT_SUCCESS));
  EXPECT_FALSE(cm_->CanLogEvents(EVENTLOG_AUDIT_FAILURE));
}

// Tests GetTestSource override.
TEST_F(ConfigManagerTest, GetTestSource_Dev) {
  CString expected_value;
#if DEBUG || !OFFICIAL_BUILD
  expected_value = kRegValueTestSourceAuto;
#endif

  CString test_source = cm_->GetTestSource();
  EXPECT_STREQ(expected_value, test_source);
  ASSERT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueTestSource,
                                   _T("dev")));
  test_source = cm_->GetTestSource();
  EXPECT_STREQ(_T("dev"), test_source);
}

TEST_F(ConfigManagerTest, GetTestSource_EmptyRegKey) {
  CString expected_value;

#if DEBUG || !OFFICIAL_BUILD
  expected_value = kRegValueTestSourceAuto;
#endif

  CString test_source = cm_->GetTestSource();
  EXPECT_STREQ(expected_value, test_source);
  ASSERT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueTestSource,
                                   _T("")));
  test_source = cm_->GetTestSource();
  EXPECT_STREQ(kRegValueTestSourceAuto, test_source);
}

//
// CanUseNetwork tests.
//

// Covers UpdateEulaAccepted case.
TEST_F(ConfigManagerTest, CanUseNetwork_Machine_Normal) {
  EXPECT_TRUE(cm_->CanUseNetwork(true));
}

// Covers UpdateEulaAccepted case.
TEST_F(ConfigManagerTest, CanUseNetwork_User_Normal) {
  EXPECT_TRUE(cm_->CanUseNetwork(false));
}

// These cover the not OEM install mode cases.
TEST_F(ConfigManagerTest, CanUseNetwork_Machine_UpdateEulaNotAccepted) {
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE,
                                    _T("eulaaccepted"),
                                    static_cast<DWORD>(0)));
  EXPECT_FALSE(cm_->CanUseNetwork(true));
}

TEST_F(ConfigManagerTest,
       CanUseNetwork_Machine_UpdateEulaNotAccepted_AppEulaAccepted) {
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE,
                                    _T("eulaaccepted"),
                                    static_cast<DWORD>(0)));
  EXPECT_SUCCEEDED(RegKey::SetValue(kAppMachineClientStatePath1,
                                    _T("eulaaccepted"),
                                    static_cast<DWORD>(1)));
  EXPECT_FALSE(cm_->CanUseNetwork(true));
}

TEST_F(ConfigManagerTest, CanUseNetwork_Machine_AppEulaNotAccepted) {
  EXPECT_SUCCEEDED(RegKey::SetValue(kAppMachineClientStatePath1,
                                    _T("eulaaccepted"),
                                    static_cast<DWORD>(0)));
  EXPECT_TRUE(cm_->CanUseNetwork(true));
}

TEST_F(ConfigManagerTest, CanUseNetwork_Machine_AppEulaAccepted) {
  EXPECT_SUCCEEDED(RegKey::SetValue(kAppMachineClientStatePath1,
                                    _T("eulaaccepted"),
                                    static_cast<DWORD>(1)));
  EXPECT_TRUE(cm_->CanUseNetwork(true));
}

TEST_F(ConfigManagerTest, CanUseNetwork_Machine_UserUpdateEulaNotAccepted) {
  EXPECT_SUCCEEDED(RegKey::SetValue(USER_REG_UPDATE,
                                    _T("eulaaccepted"),
                                    static_cast<DWORD>(0)));
  EXPECT_TRUE(cm_->CanUseNetwork(true));
}

TEST_F(ConfigManagerTest, CanUseNetwork_User_UpdateEulaNotAccepted) {
  EXPECT_SUCCEEDED(RegKey::SetValue(USER_REG_UPDATE,
                                    _T("eulaaccepted"),
                                    static_cast<DWORD>(0)));
  EXPECT_FALSE(cm_->CanUseNetwork(false));
}

TEST_F(ConfigManagerTest,
       CanUseNetwork_User_UpdateEulaNotAccepted_AppEulaAccepted) {
  EXPECT_SUCCEEDED(RegKey::SetValue(USER_REG_UPDATE,
                                    _T("eulaaccepted"),
                                    static_cast<DWORD>(0)));
  EXPECT_SUCCEEDED(RegKey::SetValue(kAppUserClientStatePath1,
                                    _T("eulaaccepted"),
                                    static_cast<DWORD>(1)));
  EXPECT_FALSE(cm_->CanUseNetwork(false));
}

TEST_F(ConfigManagerTest, CanUseNetwork_User_AppEulaNotAccepted) {
  EXPECT_SUCCEEDED(RegKey::SetValue(kAppUserClientStatePath1,
                                    _T("eulaaccepted"),
                                    static_cast<DWORD>(0)));
  EXPECT_TRUE(cm_->CanUseNetwork(false));
}

TEST_F(ConfigManagerTest, CanUseNetwork_User_AppEulaAccepted) {
  EXPECT_SUCCEEDED(RegKey::SetValue(kAppUserClientStatePath1,
                                    _T("eulaaccepted"),
                                    static_cast<DWORD>(1)));
  EXPECT_TRUE(cm_->CanUseNetwork(false));
}

TEST_F(ConfigManagerTest, CanUseNetwork_User_MachineUpdateEulaNotAccepted) {
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE,
                                    _T("eulaaccepted"),
                                    static_cast<DWORD>(0)));
  EXPECT_TRUE(cm_->CanUseNetwork(false));
}

// TODO(omaha): Figure out a way to test the result.
TEST_F(ConfigManagerTest, IsInternalUser) {
  cm_->IsInternalUser();
}

TEST_F(ConfigManagerTest, IsWindowsInstalling_Normal) {
  EXPECT_FALSE(cm_->IsWindowsInstalling());
}

// While this test passes, the return value of IsWindowsInstalling() is not
// fully tested because the account is not Administrator.
TEST_F(ConfigManagerTest, IsWindowsInstalling_Installing_Vista_InvalidValues) {
  if (!vista_util::IsVistaOrLater()) {
    return;
  }

  EXPECT_SUCCEEDED(RegKey::SetValue(
      _T("HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Setup\\State"),
      _T("ImageState"),
      _T("")));
  EXPECT_FALSE(cm_->IsWindowsInstalling());

  EXPECT_SUCCEEDED(RegKey::SetValue(
      _T("HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Setup\\State"),
      _T("ImageState"),
      _T("foo")));
  EXPECT_FALSE(cm_->IsWindowsInstalling());

  EXPECT_SUCCEEDED(RegKey::SetValue(
      _T("HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Setup\\State"),
      _T("ImageState"),
      static_cast<DWORD>(1)));
  ExpectAsserts expect_asserts;  // RegKey asserts because value type is wrong.
  EXPECT_FALSE(cm_->IsWindowsInstalling());
}

// TODO(omaha): This test fails because the account is not Administrator. Maybe
// just delete them if this is the final implementation of Audit Mode detection.
TEST_F(ConfigManagerTest, IsWindowsInstalling_Installing_Vista_ValidStates) {
  if (!vista_util::IsVistaOrLater()) {
    return;
  }

  // These states return false in the original implementation.
  EXPECT_SUCCEEDED(RegKey::SetValue(
      _T("HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Setup\\State"),
      _T("ImageState"),
      _T("IMAGE_STATE_COMPLETE")));
  EXPECT_FALSE(cm_->IsWindowsInstalling());

  EXPECT_SUCCEEDED(RegKey::SetValue(
      _T("HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Setup\\State"),
      _T("ImageState"),
      _T("IMAGE_STATE_GENERALIZE_RESEAL_TO_OOBE")));
  EXPECT_FALSE(cm_->IsWindowsInstalling());

  EXPECT_SUCCEEDED(RegKey::SetValue(
      _T("HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Setup\\State"),
      _T("ImageState"),
      _T("IMAGE_STATE_SPECIALIZE_RESEAL_TO_OOBE")));
  EXPECT_FALSE(cm_->IsWindowsInstalling());

  // These states are specified in the original implementation.
  EXPECT_SUCCEEDED(RegKey::SetValue(
      _T("HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Setup\\State"),
      _T("ImageState"),
      _T("IMAGE_STATE_UNDEPLOYABLE")));
  EXPECT_TRUE(cm_->IsWindowsInstalling());

  EXPECT_SUCCEEDED(RegKey::SetValue(
      _T("HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Setup\\State"),
      _T("ImageState"),
      _T("IMAGE_STATE_GENERALIZE_RESEAL_TO_AUDIT")));
  EXPECT_TRUE(cm_->IsWindowsInstalling());

  EXPECT_SUCCEEDED(RegKey::SetValue(
      _T("HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Setup\\State"),
      _T("ImageState"),
      _T("IMAGE_STATE_SPECIALIZE_RESEAL_TO_AUDIT")));
  EXPECT_TRUE(cm_->IsWindowsInstalling());
}

TEST_F(ConfigManagerTest, CanInstallApp_NoGroupPolicy) {
  EXPECT_TRUE(CanInstallApp(kAppGuid1));
}

TEST_F(ConfigManagerTest, CanInstallApp_DifferentAppDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(kInstallPolicyApp2, 0));
  EXPECT_TRUE(CanInstallApp(kAppGuid1));
}

TEST_F(ConfigManagerTest, CanInstallApp_NoDefaultValue_AppDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(kInstallPolicyApp1, 0));
  EXPECT_FALSE(CanInstallApp(kAppGuid1));
}

TEST_F(ConfigManagerTest, CanInstallApp_NoDefaultValue_AppEnabled) {
  EXPECT_SUCCEEDED(SetPolicy(kInstallPolicyApp1, 1));
  EXPECT_TRUE(CanInstallApp(kAppGuid1));
}

TEST_F(ConfigManagerTest, CanInstallApp_NoDefaultValue_AppInvalid) {
  EXPECT_SUCCEEDED(SetPolicy(kInstallPolicyApp1, 2));
  EXPECT_TRUE(CanInstallApp(kAppGuid1));
}

TEST_F(ConfigManagerTest, CanInstallApp_DefaultDisabled_NoAppValue) {
  EXPECT_SUCCEEDED(SetPolicy(_T("InstallDefault"), 0));
  EXPECT_FALSE(CanInstallApp(kAppGuid1));
}

TEST_F(ConfigManagerTest, CanInstallApp_DefaultDisabled_AppDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("InstallDefault"), 0));
  EXPECT_SUCCEEDED(SetPolicy(kInstallPolicyApp1, 0));
  EXPECT_FALSE(CanInstallApp(kAppGuid1));
}

TEST_F(ConfigManagerTest, CanInstallApp_DefaultDisabled_AppEnabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("InstallDefault"), 0));
  EXPECT_SUCCEEDED(SetPolicy(kInstallPolicyApp1, 1));
  EXPECT_TRUE(CanInstallApp(kAppGuid1));
}

// Invalid value defaulting to true overrides the InstallDefault disable.
TEST_F(ConfigManagerTest, CanInstallApp_DefaultDisabled_AppInvalid) {
  EXPECT_SUCCEEDED(SetPolicy(_T("InstallDefault"), 0));
  EXPECT_SUCCEEDED(SetPolicy(kInstallPolicyApp1, 2));
  EXPECT_TRUE(CanInstallApp(kAppGuid1));
}

TEST_F(ConfigManagerTest, CanInstallApp_DefaultEnabled_NoAppValue) {
  EXPECT_SUCCEEDED(SetPolicy(_T("InstallDefault"), 1));
  EXPECT_TRUE(CanInstallApp(kAppGuid1));
}

TEST_F(ConfigManagerTest, CanInstallApp_DefaultEnabled_AppDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("InstallDefault"), 1));
  EXPECT_SUCCEEDED(SetPolicy(kInstallPolicyApp1, 0));
  EXPECT_FALSE(CanInstallApp(kAppGuid1));
}

TEST_F(ConfigManagerTest, CanInstallApp_DefaultEnabled_AppEnabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("InstallDefault"), 1));
  EXPECT_SUCCEEDED(SetPolicy(kInstallPolicyApp1, 1));
  EXPECT_TRUE(CanInstallApp(kAppGuid1));
}

TEST_F(ConfigManagerTest, CanInstallApp_DefaultEnabled_AppInvalid) {
  EXPECT_SUCCEEDED(SetPolicy(_T("InstallDefault"), 1));
  EXPECT_SUCCEEDED(SetPolicy(kInstallPolicyApp1, 2));
  EXPECT_TRUE(CanInstallApp(kAppGuid1));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_NoGroupPolicy) {
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DifferentAppDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp2, 0));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DifferentAppManualOnly) {
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp2, 2));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_NoDefaultValue_AppDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 0));
  EXPECT_FALSE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_NoDefaultValue_AppEnabled) {
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 1));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_NoDefaultValue_AppManualOnly) {
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 2));
  EXPECT_FALSE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DefaultDisabled_NoAppValue) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 0));
  EXPECT_FALSE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DefaultDisabled_AppDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 0));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 0));
  EXPECT_FALSE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DefaultDisabled_AppEnabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 0));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 1));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DefaultDisabled_AppManualOnly) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 0));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 2));
  EXPECT_FALSE(CanUpdateApp(kAppGuid1, false));
}

// Invalid value defaulting to true overrides the UpdateDefault disable.
TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DefaultDisabled_AppInvalid) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 0));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 3));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DefaultEnabled_NoAppValue) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 1));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DefaultEnabled_AppDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 1));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 0));
  EXPECT_FALSE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DefaultEnabled_AppEnabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 1));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 1));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DefaultEnabled_AppManualOnly) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 1));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 2));
  EXPECT_FALSE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DefaultEnabled_AppInvalid) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 1));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 3));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DefaultManualOnly_NoAppValue) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 2));
  EXPECT_FALSE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DefaultManualOnly_AppDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 2));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 0));
  EXPECT_FALSE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DefaultManualOnly_AppEnabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 2));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 1));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DefaultManualOnly_AppManualOnly) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 2));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 2));
  EXPECT_FALSE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DefaultManualOnly_AppInvalid) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 2));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 3));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_DefaultInvalid_NoAppValue) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 3));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_Omaha_DefaultDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 0));
  EXPECT_TRUE(CanUpdateApp(kGoogleUpdateAppId, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_Omaha_DefaultManualOnly) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 2));
  EXPECT_TRUE(CanUpdateApp(kGoogleUpdateAppId, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Auto_Omaha_AppDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("Update") GOOPDATE_APP_ID, 0));
  EXPECT_TRUE(CanUpdateApp(kGoogleUpdateAppId, false));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_NoGroupPolicy) {
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DifferentAppDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp2, 0));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DifferentAppManualOnly) {
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp2, 2));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_NoDefaultValue_AppDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 0));
  EXPECT_FALSE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_NoDefaultValue_AppEnabled) {
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 1));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_NoDefaultValue_AppManualOnly) {
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 2));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DefaultDisabled_NoAppValue) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 0));
  EXPECT_FALSE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DefaultDisabled_AppDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 0));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 0));
  EXPECT_FALSE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DefaultDisabled_AppEnabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 0));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 1));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DefaultDisabled_AppManualOnly) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 0));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 2));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

// Invalid value defaulting to true overrides the UpdateDefault disable.
TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DefaultDisabled_AppInvalid) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 0));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 3));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DefaultEnabled_NoAppValue) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 1));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DefaultEnabled_AppDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 1));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 0));
  EXPECT_FALSE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DefaultEnabled_AppEnabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 1));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 1));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DefaultEnabled_AppManualOnly) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 1));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 2));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DefaultEnabled_AppInvalid) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 1));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 3));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DefaultManualOnly_NoAppValue) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 2));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DefaultManualOnly_AppDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 2));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 0));
  EXPECT_FALSE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DefaultManualOnly_AppEnabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 2));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 1));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DefaultManualOnly_AppManualOnly) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 2));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 2));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Maual_DefaultManualOnly_AppInvalid) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 2));
  EXPECT_SUCCEEDED(SetPolicy(kUpdatePolicyApp1, 3));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_DefaultInvalid_NoAppValue) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 3));
  EXPECT_TRUE(CanUpdateApp(kAppGuid1, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_Omaha_DefaultDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 0));
  EXPECT_TRUE(CanUpdateApp(kGoogleUpdateAppId, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_Omaha_DefaultManualOnly) {
  EXPECT_SUCCEEDED(SetPolicy(_T("UpdateDefault"), 2));
  EXPECT_TRUE(CanUpdateApp(kGoogleUpdateAppId, true));
}

TEST_F(ConfigManagerTest, CanUpdateApp_Manual_Omaha_AppDisabled) {
  EXPECT_SUCCEEDED(SetPolicy(_T("Update") GOOPDATE_APP_ID, 0));
  EXPECT_TRUE(CanUpdateApp(kGoogleUpdateAppId, true));
}

TEST_F(ConfigManagerTest, GetPackageCacheSizeLimitMBytes_Default) {
  EXPECT_EQ(500, cm_->GetPackageCacheSizeLimitMBytes());
}

TEST_F(ConfigManagerTest, GetPackageCacheSizeLimitMBytes_Override_TooBig) {
  EXPECT_SUCCEEDED(SetPolicy(kRegValueCacheSizeLimitMBytes, 8192));
  EXPECT_EQ(500, cm_->GetPackageCacheSizeLimitMBytes());
}

TEST_F(ConfigManagerTest, GetPackageCacheSizeLimitMBytes_Override_TooSmall) {
  EXPECT_SUCCEEDED(SetPolicy(kRegValueCacheSizeLimitMBytes, 0));
  EXPECT_EQ(500, cm_->GetPackageCacheSizeLimitMBytes());
}

TEST_F(ConfigManagerTest, GetPackageCacheSizeLimitMBytes_Override_Valid) {
  EXPECT_SUCCEEDED(SetPolicy(kRegValueCacheSizeLimitMBytes, 250));
  EXPECT_EQ(250, cm_->GetPackageCacheSizeLimitMBytes());
}

TEST_F(ConfigManagerTest, GetPackageCacheExpirationTimeDays_Default) {
  EXPECT_EQ(180, cm_->GetPackageCacheExpirationTimeDays());
}

TEST_F(ConfigManagerTest, GetPackageCacheExpirationTimeDays_Override_TooBig) {
  EXPECT_SUCCEEDED(SetPolicy(kRegValueCacheLifeLimitDays, 3600));
  EXPECT_EQ(180, cm_->GetPackageCacheExpirationTimeDays());
}

TEST_F(ConfigManagerTest, GetPackageCacheExpirationTimeDays_Override_TooSmall) {
  EXPECT_SUCCEEDED(SetPolicy(kRegValueCacheLifeLimitDays, 0));
  EXPECT_EQ(180, cm_->GetPackageCacheExpirationTimeDays());
}

TEST_F(ConfigManagerTest, GetPackageCacheExpirationTimeDays_Override_Valid) {
  EXPECT_SUCCEEDED(SetPolicy(kRegValueCacheLifeLimitDays, 60));
  EXPECT_EQ(60, cm_->GetPackageCacheExpirationTimeDays());
}

TEST_F(ConfigManagerTest, LastCheckedTime) {
  DWORD time = 500;
  EXPECT_SUCCEEDED(cm_->SetLastCheckedTime(true, time));
  EXPECT_EQ(time, cm_->GetLastCheckedTime(true));

  time = 77003;
  EXPECT_SUCCEEDED(cm_->SetLastCheckedTime(false, time));
  EXPECT_EQ(time, cm_->GetLastCheckedTime(false));
}

// Tests GetDir indirectly.
TEST_F(ConfigManagerTest, GetDir) {
  RestoreRegistryHives();

  CString user_install_dir = cm_->GetUserGoopdateInstallDir();
  CString user_profile;
  ASSERT_NE(0, ::GetEnvironmentVariable(_T("USERPROFILE"),
                                        CStrBuf(user_profile, MAX_PATH),
                                        MAX_PATH));
  ASSERT_TRUE(String_StartsWith(user_install_dir, user_profile, true));
}

TEST_F(ConfigManagerTest, GetUpdateWorkerStartUpDelayMs_Repeated) {
  if (!SystemInfo::IsRunningOnXPOrLater()) {
    std::wcout << _T("\tTest did not run because GenRandom breaks on Windows ")
               << _T("2000 if the registry keys are overridden.") << std::endl;
    return;
  }

  // Test the UpdateDelay multiple times.
  for (int i = 0; i < 10; ++i) {
    int random = cm_->GetUpdateWorkerStartUpDelayMs();
    EXPECT_GE(random, kUpdateTimerStartupDelayMinMs);
    EXPECT_LE(random, kUpdateTimerStartupDelayMaxMs);
  }
}

TEST_F(ConfigManagerTest, GetUpdateWorkerStartUpDelayMs) {
  if (!SystemInfo::IsRunningOnXPOrLater()) {
    std::wcout << _T("\tTest did not run because GenRandom breaks on Windows ")
               << _T("2000 if the registry keys are overridden.") << std::endl;
    return;
  }

  int random = cm_->GetUpdateWorkerStartUpDelayMs();
  EXPECT_GE(random, kUpdateTimerStartupDelayMinMs);
  EXPECT_LE(random, kUpdateTimerStartupDelayMaxMs);

  int num_times_to_try_for_diff_number = 3;
  // We run the method num_times_to_try_for_diff_number times to make
  // sure that at least one of these returns a number that is different
  // from the one that is returned above. This is needed, since the
  // method returns a number between kUpdateTimerStartupDelayMinMs and
  // kUpdateTimerStartupDelayMaxMs.
  // If this fails a lot we should disable the if check below.
  bool found_one_not_equal = false;
  for (int i = 0; i < num_times_to_try_for_diff_number; ++i) {
    int random_compare = cm_->GetUpdateWorkerStartUpDelayMs();

    EXPECT_GE(random_compare, kUpdateTimerStartupDelayMinMs);
    EXPECT_LE(random_compare, kUpdateTimerStartupDelayMaxMs);

    if (random_compare != random) {
      found_one_not_equal = true;
      break;
    }
  }

  EXPECT_TRUE(found_one_not_equal);
}

TEST_F(ConfigManagerTest, GetUpdateWorkerStartUpDelayMs_Override) {
  // Test that the initial delay time to launch a worker can be overriden.
  DWORD val = 3320;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueAuCheckPeriodMs,
                                    val));

  int random = cm_->GetUpdateWorkerStartUpDelayMs();
  EXPECT_EQ(val, random);
}

TEST_F(ConfigManagerTest, GetTimeSinceLastCheckedSec_User) {
  // First, there is no value present in the registry.
  uint32 now_sec = Time64ToInt32(GetCurrent100NSTime());
  int time_since_last_checked_sec = cm_->GetTimeSinceLastCheckedSec(false);
  EXPECT_EQ(now_sec, time_since_last_checked_sec);

  // Second, write the 'now' time.
  EXPECT_HRESULT_SUCCEEDED(cm_->SetLastCheckedTime(false, now_sec));
  time_since_last_checked_sec = cm_->GetTimeSinceLastCheckedSec(false);
  EXPECT_EQ(0, time_since_last_checked_sec);
}

TEST_F(ConfigManagerTest, GetTimeSinceLastCheckedSec_Machine) {
  uint32 now_sec = Time64ToInt32(GetCurrent100NSTime());
  int time_since_last_checked_sec = cm_->GetTimeSinceLastCheckedSec(true);
  EXPECT_EQ(now_sec, time_since_last_checked_sec);

  EXPECT_HRESULT_SUCCEEDED(cm_->SetLastCheckedTime(true, now_sec));
  time_since_last_checked_sec = cm_->GetTimeSinceLastCheckedSec(true);
  EXPECT_EQ(0, time_since_last_checked_sec);
}

TEST_F(ConfigManagerTest, GetNetConfig) {
  CString actual_value;
  EXPECT_HRESULT_FAILED(cm_->GetNetConfig(&actual_value));

  const CString expected_value = _T("proxy:8080");
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueNetConfig,
                                    expected_value));

  EXPECT_HRESULT_SUCCEEDED(cm_->GetNetConfig(&actual_value));
  EXPECT_STREQ(expected_value, actual_value);
}

TEST_F(ConfigManagerTest, GetInstallTime) {
  EXPECT_SUCCEEDED(DeleteUpdateTime(false));
  EXPECT_SUCCEEDED(DeleteFirstInstallTime(false));
  EXPECT_EQ(0, ConfigManager::GetInstallTime(false));

  DWORD time = 500;
  EXPECT_SUCCEEDED(SetFirstInstallTime(false, time));
  EXPECT_EQ(time, ConfigManager::GetInstallTime(false));

  time = 1000;
  EXPECT_SUCCEEDED(SetUpdateTime(false, time));
  EXPECT_EQ(time, ConfigManager::GetInstallTime(false));

  EXPECT_SUCCEEDED(DeleteFirstInstallTime(false));
  EXPECT_EQ(time, ConfigManager::GetInstallTime(false));
}

TEST_F(ConfigManagerTest, Is24HoursSinceInstall) {
  const uint32 now = Time64ToInt32(GetCurrent100NSTime());
  const int k12HourPeriodSec = 12 * 60 * 60;
  const int k48HourPeriodSec = 48 * 60 * 60;

  const uint32 first_install_12 = now - k12HourPeriodSec;
  const uint32 first_install_48 = now - k48HourPeriodSec;

  EXPECT_SUCCEEDED(SetFirstInstallTime(false, first_install_12));
  EXPECT_FALSE(ConfigManager::Is24HoursSinceInstall(false));

  EXPECT_SUCCEEDED(SetFirstInstallTime(false, first_install_48));
  EXPECT_TRUE(ConfigManager::Is24HoursSinceInstall(false));

  EXPECT_SUCCEEDED(SetUpdateTime(false, first_install_12));
  EXPECT_FALSE(ConfigManager::Is24HoursSinceInstall(false));

  EXPECT_SUCCEEDED(SetUpdateTime(false, first_install_48));
  EXPECT_TRUE(ConfigManager::Is24HoursSinceInstall(false));
}

TEST_F(ConfigManagerTest, AlwaysAllowCrashUploads) {
  EXPECT_FALSE(cm_->AlwaysAllowCrashUploads());

  DWORD value = 1;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueAlwaysAllowCrashUploads,
                                    value));

  EXPECT_TRUE(cm_->AlwaysAllowCrashUploads());

  value = 0;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_UPDATE_DEV,
                                    kRegValueAlwaysAllowCrashUploads,
                                    value));

  EXPECT_FALSE(cm_->AlwaysAllowCrashUploads());
}

}  // namespace omaha
