// Copyright 2008-2009 Google Inc.
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

#include <windows.h>
#include "omaha/common/app_util.h"
#include "omaha/common/error.h"
#include "omaha/common/file.h"
#include "omaha/common/path.h"
#include "omaha/common/scoped_ptr_address.h"
#include "omaha/common/time.h"
#include "omaha/goopdate/const_goopdate.h"
#include "omaha/testing/unit_test.h"
#include "omaha/worker/job_creator.h"
#include "omaha/worker/ping.h"
#include "omaha/worker/worker_metrics.h"

namespace omaha {

namespace {

const TCHAR* kGuidApp1 = _T("{CDABE316-39CD-43BA-8440-6D1E0547AEE6}");
const TCHAR* kGuidApp2 = _T("{28A93830-1746-4F0B-90F5-CF44B41169F3}");
const TCHAR* kGuidApp3 = _T("{E5D3562E-BFAE-48c6-B9C5-4E293F695E0E}");
const TCHAR* kGuidApp4 = _T("{F9346563-85DA-4dc1-A621-FAF6F869680A}");

const TCHAR* const kApp1ClientStateKeyPathMachine =
    _T("HKLM\\Software\\Google\\Update\\ClientState\\")
    _T("{CDABE316-39CD-43BA-8440-6D1E0547AEE6}");
const TCHAR* const kApp2ClientStateKeyPathMachine =
    _T("HKLM\\Software\\Google\\Update\\ClientState\\")
    _T("{28A93830-1746-4F0B-90F5-CF44B41169F3}");
const TCHAR* const kApp3ClientStateKeyPathMachine =
    _T("HKLM\\Software\\Google\\Update\\ClientState\\")
    _T("{E5D3562E-BFAE-48c6-B9C5-4E293F695E0E}");
const TCHAR* const kApp4ClientStateKeyPathMachine =
    _T("HKLM\\Software\\Google\\Update\\ClientState\\")
    _T("{F9346563-85DA-4dc1-A621-FAF6F869680A}");
const TCHAR* const kApp1ClientsKeyPathMachine =
    _T("HKLM\\Software\\Google\\Update\\Clients\\")
    _T("{CDABE316-39CD-43BA-8440-6D1E0547AEE6}");
const TCHAR* const kApp2ClientsKeyPathMachine =
    _T("HKLM\\Software\\Google\\Update\\Clients\\")
    _T("{28A93830-1746-4F0B-90F5-CF44B41169F3}");

}  // namespace

class JobCreatorTest : public testing::Test {
 protected:
  JobCreatorTest()
      : app1_guid_(StringToGuid(kGuidApp1)),
        app2_guid_(StringToGuid(kGuidApp2)),
        app3_guid_(StringToGuid(kGuidApp3)),
        app4_guid_(StringToGuid(kGuidApp4)) {
  }

  virtual void SetUp() {
    RegKey::DeleteKey(kRegistryHiveOverrideRoot);
    OverrideRegistryHives(kRegistryHiveOverrideRoot);

    metric_worker_skipped_app_update_for_self_update.Reset();
  }

  virtual void TearDown() {
    RestoreRegistryHives();
    RegKey::DeleteKey(kRegistryHiveOverrideRoot);
  }

  CompletionInfo UpdateResponseDataToCompletionInfo(
      const UpdateResponseData& response_data,
      const CString& display_name) {
    JobCreator job_creator(false, false, &ping_);
    job_creator.set_fail_if_update_not_available(true);
    return job_creator.UpdateResponseDataToCompletionInfo(response_data,
                                                          display_name);
  }

  static HRESULT CallFindOfflineFilePath(const CString& offline_dir,
                                         const CString& app_guid,
                                         CString* file_path) {
    return JobCreator::FindOfflineFilePath(offline_dir, app_guid, file_path);
  }

  static HRESULT CallReadOfflineManifest(const CString& offline_dir,
                                         const CString& app_guid,
                                         UpdateResponse* response) {
    return JobCreator::ReadOfflineManifest(offline_dir, app_guid, response);
  }

  Ping ping_;
  const GUID app1_guid_;
  const GUID app2_guid_;
  const GUID app3_guid_;
  const GUID app4_guid_;
};

TEST_F(JobCreatorTest, CreateJobsFromResponses_UpdateMultipleAppsAndStatuses) {
  const CString version_goopdate = _T("1.2.75.3");
  const CString version_app1 = _T("1.1.2.3");
  const CString version_app2 = _T("2.0.0.5");

  JobCreator job_creator(true, true, &ping_);
  job_creator.set_is_auto_update(true);
  AppManager app_manager(true);

  const DWORD kExistingUpdateValues = 0x70123456;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_CLIENT_STATE_GOOPDATE,
                                    kRegValueLastSuccessfulCheckSec,
                                    kExistingUpdateValues));
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_CLIENT_STATE_GOOPDATE,
                                    kRegValueLastUpdateTimeSec,
                                    kExistingUpdateValues));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp1ClientStateKeyPathMachine,
                                    kRegValueLastSuccessfulCheckSec,
                                    kExistingUpdateValues));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp1ClientStateKeyPathMachine,
                                    kRegValueLastUpdateTimeSec,
                                    kExistingUpdateValues));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp2ClientStateKeyPathMachine,
                                    kRegValueLastSuccessfulCheckSec,
                                    kExistingUpdateValues));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp2ClientStateKeyPathMachine,
                                    kRegValueLastUpdateTimeSec,
                                    kExistingUpdateValues));
  EXPECT_SUCCEEDED(
      RegKey::SetValue(kApp2ClientsKeyPathMachine, _T("pv"), version_app2));

  ProductDataVector products;

  AppData app_data_omaha;
  app_data_omaha.set_app_guid(kGoopdateGuid);
  app_data_omaha.set_is_machine_app(true);
  app_data_omaha.set_version(version_goopdate);
  ProductData product_omaha(app_data_omaha);
  products.push_back(product_omaha);

  AppData app_data1;
  app_data1.set_app_guid(app1_guid_);
  app_data1.set_is_machine_app(true);
  app_data1.set_version(version_app1);
  ProductData product1(app_data1);
  products.push_back(product1);

  AppData app_data2;
  app_data2.set_app_guid(app2_guid_);
  app_data2.set_is_machine_app(true);
  app_data2.set_version(version_app2);
  ProductData product2(app_data2);
  products.push_back(product2);

  UpdateResponses responses;

  UpdateResponseData resp_data_omaha;
  resp_data_omaha.set_guid(kGoopdateGuid);
  resp_data_omaha.set_needs_admin(NEEDS_ADMIN_YES);
  resp_data_omaha.set_status(kResponseStatusNoUpdate);
  UpdateResponse resp_omaha(resp_data_omaha);
  responses.insert(std::pair<GUID, UpdateResponse>(kGoopdateGuid, resp_omaha));

  UpdateResponseData resp_data1;
  resp_data1.set_guid(app1_guid_);
  resp_data1.set_needs_admin(NEEDS_ADMIN_YES);
  resp_data1.set_status(kResponseStatusNoUpdate);
  UpdateResponse resp1(resp_data1);
  responses.insert(std::pair<GUID, UpdateResponse>(app1_guid_, resp1));

  UpdateResponseData resp_data2;
  resp_data2.set_guid(app2_guid_);
  resp_data2.set_needs_admin(NEEDS_ADMIN_YES);
  resp_data2.set_status(kResponseStatusOkValue);
  // TODO(omaha): Add component responses here.
  UpdateResponse resp2(resp_data2);
  responses.insert(std::pair<GUID, UpdateResponse>(app2_guid_, resp2));

  Jobs jobs;
  Request ping_request(true);
  CString event_log_text;
  CompletionInfo completion_info;

  // This should succeed for an update since it's OK to have "no update
  // available" for updates.
  EXPECT_SUCCEEDED(job_creator.CreateJobsFromResponses(responses,
                                                       products,
                                                       &jobs,
                                                       &ping_request,
                                                       &event_log_text,
                                                       &completion_info));
  const uint32 now = Time64ToInt32(GetCurrent100NSTime());

  // Should be a job for Resp2 since Resp1 was status "No Update".
  ASSERT_EQ(1, jobs.size());
  EXPECT_EQ(3, ping_request.get_request_count());
  EXPECT_EQ(COMPLETION_SUCCESS, completion_info.status);
  EXPECT_EQ(0, completion_info.error_code);
  EXPECT_TRUE(completion_info.text.IsEmpty());
  EXPECT_FALSE(jobs[0]->is_offline());

  // Sleep so that there is a time difference between the time written in the
  // registry and now.
  ::Sleep(20);

  // Omaha.
  // Update Stats should not have been set because there was no update.
  // Successful update check set for noupdate but successful update not updated.
  DWORD update_responses(1);
  DWORD64 time_since_first_response_ms(1);
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       kGoopdateGuid,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(0, update_responses);
  EXPECT_EQ(0, time_since_first_response_ms);

  const uint32 last_check_sec_omaha =
      GetDwordValue(MACHINE_REG_CLIENT_STATE_GOOPDATE,
                    kRegValueLastSuccessfulCheckSec);
  EXPECT_NE(kExistingUpdateValues, last_check_sec_omaha);
  EXPECT_GE(now, last_check_sec_omaha);
  EXPECT_GE(static_cast<uint32>(200), now - last_check_sec_omaha);
  EXPECT_EQ(kExistingUpdateValues,
            GetDwordValue(MACHINE_REG_CLIENT_STATE_GOOPDATE,
                          kRegValueLastUpdateTimeSec));

  // App 1.
  // Update Stats should not have been set because there was no update.
  // Successful update check set for noupdate but successful update not updated.
  update_responses = 1;
  time_since_first_response_ms = 1;
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       app1_guid_,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(0, update_responses);
  EXPECT_EQ(0, time_since_first_response_ms);

  const uint32 last_check_sec_app1 =
      GetDwordValue(kApp1ClientStateKeyPathMachine,
                    kRegValueLastSuccessfulCheckSec);
  EXPECT_NE(kExistingUpdateValues, last_check_sec_app1);
  EXPECT_GE(now, last_check_sec_app1);
  EXPECT_GE(static_cast<uint32>(200), now - last_check_sec_app1);
  EXPECT_EQ(kExistingUpdateValues,
            GetDwordValue(kApp1ClientStateKeyPathMachine,
                          kRegValueLastUpdateTimeSec));

  // App 2.
  // Update Stats should have been set.
  // Neither successful update check nor successful update are updated because
  // the update has not been completed.
  update_responses = 0;
  time_since_first_response_ms = 0;
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       app2_guid_,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(1, update_responses);
  EXPECT_LT(0, time_since_first_response_ms);
  EXPECT_GT(10 * kMsPerSec, time_since_first_response_ms);

  EXPECT_EQ(kExistingUpdateValues,
            GetDwordValue(kApp2ClientStateKeyPathMachine,
                          kRegValueLastSuccessfulCheckSec));
  EXPECT_EQ(kExistingUpdateValues,
            GetDwordValue(kApp2ClientStateKeyPathMachine,
                          kRegValueLastUpdateTimeSec));
}

TEST_F(JobCreatorTest, CreateJobsFromResponses_UpdateForUpdateDisabledApp) {
  const CString version_goopdate = _T("1.2.75.3");
  const CString version_app1 = _T("1.1.2.3");
  const CString version_app2 = _T("2.0.0.5");
  const CString version_app3 = _T("11.0.0.5");
  const CString version_app4 = _T("5.0.6.7");

  JobCreator job_creator(true, true, &ping_);
  job_creator.set_is_auto_update(true);
  AppManager app_manager(true);

  const DWORD kExistingUpdateValues = 0x70123456;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_CLIENT_STATE_GOOPDATE,
                                    kRegValueLastSuccessfulCheckSec,
                                    kExistingUpdateValues));
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_CLIENT_STATE_GOOPDATE,
                                    kRegValueLastUpdateTimeSec,
                                    kExistingUpdateValues));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp1ClientStateKeyPathMachine,
                                    kRegValueLastSuccessfulCheckSec,
                                    kExistingUpdateValues));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp1ClientStateKeyPathMachine,
                                    kRegValueLastUpdateTimeSec,
                                    kExistingUpdateValues));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp2ClientStateKeyPathMachine,
                                    kRegValueLastSuccessfulCheckSec,
                                    kExistingUpdateValues));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp2ClientStateKeyPathMachine,
                                    kRegValueLastUpdateTimeSec,
                                    kExistingUpdateValues));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp3ClientStateKeyPathMachine,
                                    kRegValueLastSuccessfulCheckSec,
                                    kExistingUpdateValues));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp3ClientStateKeyPathMachine,
                                    kRegValueLastUpdateTimeSec,
                                    kExistingUpdateValues));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp4ClientStateKeyPathMachine,
                                    kRegValueLastSuccessfulCheckSec,
                                    kExistingUpdateValues));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp4ClientStateKeyPathMachine,
                                    kRegValueLastUpdateTimeSec,
                                    kExistingUpdateValues));
  EXPECT_SUCCEEDED(
      RegKey::SetValue(kApp1ClientsKeyPathMachine, _T("pv"), version_app1));
  EXPECT_SUCCEEDED(
      RegKey::SetValue(kApp2ClientsKeyPathMachine, _T("pv"), version_app2));

  // Required for testing deletion of this data when updates are disabled (app1)
  // and noupdate is returned (Omaha and app3).
  const DWORD64 kUpdateAvailableSince =
    GetCurrent100NSTime() - 200 * kMsPerSec * kMillisecsTo100ns;
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_CLIENT_STATE_GOOPDATE,
                                    kRegValueUpdateAvailableCount,
                                    static_cast<DWORD>(123456)));
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_CLIENT_STATE_GOOPDATE,
                                    kRegValueUpdateAvailableSince,
                                    kUpdateAvailableSince));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp1ClientStateKeyPathMachine,
                                    kRegValueUpdateAvailableCount,
                                    static_cast<DWORD>(123)));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp1ClientStateKeyPathMachine,
                                    kRegValueUpdateAvailableSince,
                                    kUpdateAvailableSince));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp2ClientStateKeyPathMachine,
                                    kRegValueUpdateAvailableCount,
                                    static_cast<DWORD>(2345)));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp2ClientStateKeyPathMachine,
                                    kRegValueUpdateAvailableSince,
                                    kUpdateAvailableSince));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp3ClientStateKeyPathMachine,
                                    kRegValueUpdateAvailableCount,
                                    static_cast<DWORD>(456)));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp3ClientStateKeyPathMachine,
                                    kRegValueUpdateAvailableSince,
                                    kUpdateAvailableSince));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp4ClientStateKeyPathMachine,
                                    kRegValueUpdateAvailableCount,
                                    static_cast<DWORD>(98)));
  EXPECT_SUCCEEDED(RegKey::SetValue(kApp4ClientStateKeyPathMachine,
                                    kRegValueUpdateAvailableSince,
                                    kUpdateAvailableSince));
  // Verify the data is set correctly.
  DWORD update_responses(0);
  DWORD64 time_since_first_response_ms(0);
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       kGoopdateGuid,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(123456, update_responses);
  EXPECT_LE(200000, time_since_first_response_ms);
  update_responses = 0;
  time_since_first_response_ms = 0;
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       StringToGuid(kGuidApp1),
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(123, update_responses);
  EXPECT_LE(200000, time_since_first_response_ms);
  update_responses = 0;
  time_since_first_response_ms = 0;
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       StringToGuid(kGuidApp2),
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(2345, update_responses);
  EXPECT_LE(200000, time_since_first_response_ms);
  update_responses = 0;
  time_since_first_response_ms = 0;
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       StringToGuid(kGuidApp3),
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(456, update_responses);
  EXPECT_LE(200000, time_since_first_response_ms);
  update_responses = 0;
  time_since_first_response_ms = 0;
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       StringToGuid(kGuidApp4),
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(98, update_responses);
  EXPECT_LE(200000, time_since_first_response_ms);

  ProductDataVector products;

  AppData app_data_omaha;
  app_data_omaha.set_app_guid(kGoopdateGuid);
  app_data_omaha.set_is_machine_app(true);
  app_data_omaha.set_version(version_goopdate);
  ProductData product_omaha(app_data_omaha);
  products.push_back(product_omaha);

  AppData app_data1;
  app_data1.set_app_guid(app1_guid_);
  app_data1.set_is_machine_app(true);
  app_data1.set_version(version_app1);
  app_data1.set_is_update_disabled(true);
  ProductData product1(app_data1);
  products.push_back(product1);

  AppData app_data2;
  app_data2.set_app_guid(app2_guid_);
  app_data2.set_is_machine_app(true);
  app_data2.set_version(version_app2);
  ProductData product2(app_data2);
  products.push_back(product2);

  AppData app_data3;
  app_data3.set_app_guid(app3_guid_);
  app_data3.set_is_machine_app(true);
  app_data3.set_version(version_app3);
  app_data3.set_is_update_disabled(true);
  ProductData product3(app_data3);
  products.push_back(product3);

  AppData app_data4;
  app_data4.set_app_guid(app4_guid_);
  app_data4.set_is_machine_app(true);
  app_data4.set_version(version_app4);
  ProductData product4(app_data4);
  products.push_back(product4);

  UpdateResponses responses;

  UpdateResponseData resp_data_omaha;
  resp_data_omaha.set_guid(kGoopdateGuid);
  resp_data_omaha.set_needs_admin(NEEDS_ADMIN_YES);
  resp_data_omaha.set_status(kResponseStatusNoUpdate);
  UpdateResponse resp_omaha(resp_data_omaha);
  responses.insert(std::pair<GUID, UpdateResponse>(kGoopdateGuid, resp_omaha));

  UpdateResponseData resp_data1;
  resp_data1.set_guid(app1_guid_);
  resp_data1.set_needs_admin(NEEDS_ADMIN_YES);
  resp_data1.set_status(kResponseStatusOkValue);
  UpdateResponse resp1(resp_data1);
  responses.insert(std::pair<GUID, UpdateResponse>(app1_guid_, resp1));

  UpdateResponseData resp_data2;
  resp_data2.set_guid(app2_guid_);
  resp_data2.set_needs_admin(NEEDS_ADMIN_YES);
  resp_data2.set_status(kResponseStatusOkValue);
  UpdateResponse resp2(resp_data2);
  responses.insert(std::pair<GUID, UpdateResponse>(app2_guid_, resp2));

  UpdateResponseData resp_data3;
  resp_data3.set_guid(app3_guid_);
  resp_data3.set_needs_admin(NEEDS_ADMIN_YES);
  resp_data3.set_status(kResponseStatusNoUpdate);
  UpdateResponse resp3(resp_data3);
  responses.insert(std::pair<GUID, UpdateResponse>(app3_guid_, resp3));

  UpdateResponseData resp_data4;
  resp_data4.set_guid(app4_guid_);
  resp_data4.set_needs_admin(NEEDS_ADMIN_YES);
  resp_data4.set_status(kResponseStatusInternalError);
  UpdateResponse resp4(resp_data4);
  responses.insert(std::pair<GUID, UpdateResponse>(app4_guid_, resp4));

  Jobs jobs;
  Request ping_request(true);
  CString event_log_text;
  CompletionInfo completion_info;

  EXPECT_SUCCEEDED(job_creator.CreateJobsFromResponses(responses,
                                                       products,
                                                       &jobs,
                                                       &ping_request,
                                                       &event_log_text,
                                                       &completion_info));
  const uint32 now = Time64ToInt32(GetCurrent100NSTime());

  // Should be a job for Resp2 only since Resp1 was had updates disabled.
  ASSERT_EQ(1, jobs.size());
  // Pings for the update available app, the error-internal app, and the
  // noupdate apps. No ping for the disabled app with update available.
  // Not sure why the noupdate apps get a ping.
  EXPECT_EQ(4, ping_request.get_request_count());
  EXPECT_EQ(COMPLETION_SUCCESS, completion_info.status);
  EXPECT_EQ(0, completion_info.error_code);
  EXPECT_TRUE(completion_info.text.IsEmpty());
  EXPECT_FALSE(jobs[0]->is_offline());

  // Sleep so that there is a time difference between the time written in the
  // registry and now.
  ::Sleep(20);

  // Omaha.
  // Update Stats should have been cleared because there was no update - we
  // should not keep around old data if there is no update to apply.
  // Successful update check set for noupdate but successful update not updated.
  update_responses = 1;
  time_since_first_response_ms = 1;
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       kGoopdateGuid,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(0, update_responses);
  EXPECT_EQ(0, time_since_first_response_ms);

  const uint32 last_check_sec_omaha =
      GetDwordValue(MACHINE_REG_CLIENT_STATE_GOOPDATE,
                    kRegValueLastSuccessfulCheckSec);
  EXPECT_NE(kExistingUpdateValues, last_check_sec_omaha);
  EXPECT_GE(now, last_check_sec_omaha);
  EXPECT_GE(static_cast<uint32>(200), now - last_check_sec_omaha);
  EXPECT_EQ(kExistingUpdateValues,
            GetDwordValue(MACHINE_REG_CLIENT_STATE_GOOPDATE,
                          kRegValueLastUpdateTimeSec));

  // App 1.
  // Update Stats should have been cleared because these values are used to
  // anaylze the success of Omaha and disabled updates would break these stats.
  // Successful update check and successful update are not set because and
  // update is available but disabled.
  update_responses = 1;
  time_since_first_response_ms = 1;
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       app1_guid_,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(0, update_responses);
  EXPECT_EQ(0, time_since_first_response_ms);

  EXPECT_EQ(kExistingUpdateValues,
            GetDwordValue(kApp1ClientStateKeyPathMachine,
                          kRegValueLastSuccessfulCheckSec));
  EXPECT_EQ(kExistingUpdateValues,
            GetDwordValue(kApp1ClientStateKeyPathMachine,
                          kRegValueLastUpdateTimeSec));

  // App 2.
  // Update Stats: responses should have been incremented and time since first
  // response should be based on the value set above.
  // Neither successful update check nor successful update are updated because
  // the update has not been completed.
  update_responses = 0;
  time_since_first_response_ms = 0;
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       app2_guid_,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(2346, update_responses);
  EXPECT_LT(200 * kMsPerSec + 20, time_since_first_response_ms);
  EXPECT_GT(202 * kMsPerSec, time_since_first_response_ms);

  EXPECT_EQ(kExistingUpdateValues,
            GetDwordValue(kApp2ClientStateKeyPathMachine,
                          kRegValueLastSuccessfulCheckSec));
  EXPECT_EQ(kExistingUpdateValues,
            GetDwordValue(kApp2ClientStateKeyPathMachine,
                          kRegValueLastUpdateTimeSec));

  // App 3.
  // Update Stats should have been cleared because there was no update - we
  // should not keep around old data if there is no update to apply.
  // Successful update check set for noupdate but successful update not updated.
  update_responses = 1;
  time_since_first_response_ms = 1;
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       app3_guid_,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(0, update_responses);
  EXPECT_EQ(0, time_since_first_response_ms);
  const uint32 last_check_sec_app3 =
      GetDwordValue(kApp3ClientStateKeyPathMachine,
                    kRegValueLastSuccessfulCheckSec);
  EXPECT_NE(kExistingUpdateValues, last_check_sec_app3);
  EXPECT_GE(now, last_check_sec_app3);
  EXPECT_GE(static_cast<uint32>(200), now - last_check_sec_app3);
  EXPECT_EQ(kExistingUpdateValues,
            GetDwordValue(kApp3ClientStateKeyPathMachine,
                          kRegValueLastUpdateTimeSec));

  // App 4.
  // Update Stats: responses should have not been incremented and time since
  // first response should be based on the value set above.
  // Neither successful update check nor successful update are updated because
  // the update has not been completed.
  update_responses = 0;
  time_since_first_response_ms = 0;
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       app4_guid_,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(98, update_responses);
  EXPECT_LT(200 * kMsPerSec + 20, time_since_first_response_ms);
  EXPECT_GT(202 * kMsPerSec, time_since_first_response_ms);

  EXPECT_EQ(kExistingUpdateValues,
            GetDwordValue(kApp4ClientStateKeyPathMachine,
                          kRegValueLastSuccessfulCheckSec));
  EXPECT_EQ(kExistingUpdateValues,
            GetDwordValue(kApp4ClientStateKeyPathMachine,
                          kRegValueLastUpdateTimeSec));
}

// This should fail for an install since it's not OK to have "no update
// available" for clean installs
TEST_F(JobCreatorTest, CreateJobsFromResponses_InstallFailure) {
  const CString version_app1 = _T("1.1.2.3");
  const CString version_app2 = _T("2.0.0.5");

  JobCreator job_creator(true, false, &ping_);
  job_creator.set_fail_if_update_not_available(true);
  AppManager app_manager(true);

  EXPECT_SUCCEEDED(RegKey::CreateKey(kApp1ClientStateKeyPathMachine));
  EXPECT_SUCCEEDED(RegKey::CreateKey(kApp2ClientStateKeyPathMachine));

  ProductDataVector products;
  AppData app_data1;
  app_data1.set_app_guid(app1_guid_);
  app_data1.set_is_machine_app(true);
  app_data1.set_version(version_app1);
  ProductData product1(app_data1);
  products.push_back(product1);

  AppData app_data2;
  app_data2.set_app_guid(app2_guid_);
  app_data2.set_is_machine_app(true);
  app_data2.set_version(version_app2);
  ProductData product2(app_data2);
  products.push_back(product2);

  UpdateResponses responses;

  UpdateResponseData resp_data1;
  resp_data1.set_guid(app1_guid_);
  resp_data1.set_needs_admin(NEEDS_ADMIN_YES);
  resp_data1.set_status(kResponseStatusNoUpdate);
  UpdateResponse resp1(resp_data1);
  responses.insert(std::pair<GUID, UpdateResponse>(app1_guid_, resp1));

  UpdateResponseData resp_data2;
  resp_data2.set_guid(app2_guid_);
  resp_data2.set_needs_admin(NEEDS_ADMIN_YES);
  resp_data2.set_status(kResponseStatusOkValue);
  // TODO(omaha): Add component responses here.
  UpdateResponse resp2(resp_data2);
  responses.insert(std::pair<GUID, UpdateResponse>(app2_guid_, resp2));

  Jobs jobs;
  Request ping_request(true);
  CString event_log_text;
  CompletionInfo completion_info;
  EXPECT_FAILED(job_creator.CreateJobsFromResponses(responses,
                                                    products,
                                                    &jobs,
                                                    &ping_request,
                                                    &event_log_text,
                                                    &completion_info));

  EXPECT_EQ(0, jobs.size());
  EXPECT_EQ(1, ping_request.get_request_count());
  EXPECT_EQ(GOOPDATE_E_NO_UPDATE_RESPONSE, completion_info.error_code);
  EXPECT_EQ(COMPLETION_ERROR, completion_info.status);
  EXPECT_STREQ(_T("No update is available."), completion_info.text);

  // Sleep so that there would be a time difference between the time written in
  // the registry and now.
  ::Sleep(20);

  // There should not be any data because this is an install.
  DWORD update_responses(1);
  DWORD64 time_since_first_response_ms(1);
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       app1_guid_,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(0, update_responses);
  EXPECT_EQ(0, time_since_first_response_ms);

  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       app2_guid_,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(0, update_responses);
  EXPECT_EQ(0, time_since_first_response_ms);

  EXPECT_FALSE(RegKey::HasValue(kApp1ClientStateKeyPathMachine,
                                kRegValueLastSuccessfulCheckSec));
  EXPECT_FALSE(RegKey::HasValue(kApp1ClientStateKeyPathMachine,
                                kRegValueLastUpdateTimeSec));
  EXPECT_FALSE(RegKey::HasValue(kApp2ClientStateKeyPathMachine,
                                kRegValueLastSuccessfulCheckSec));
  EXPECT_FALSE(RegKey::HasValue(kApp2ClientStateKeyPathMachine,
                                kRegValueLastUpdateTimeSec));
}

TEST_F(JobCreatorTest, CreateJobsFromResponses_InstallSuccess) {
  const CString version_app1 = _T("1.1.2.3");
  const CString version_app2 = _T("2.0.0.5");

  JobCreator job_creator(true, false, &ping_);
  job_creator.set_fail_if_update_not_available(true);
  AppManager app_manager(true);

  EXPECT_SUCCEEDED(RegKey::CreateKey(kApp1ClientStateKeyPathMachine));
  EXPECT_SUCCEEDED(RegKey::CreateKey(kApp2ClientStateKeyPathMachine));

  ProductDataVector products;
  AppData app_data1;
  app_data1.set_app_guid(app1_guid_);
  app_data1.set_is_machine_app(true);
  app_data1.set_version(version_app1);
  ProductData product1(app_data1);
  products.push_back(product1);

  AppData app_data2;
  app_data2.set_app_guid(app2_guid_);
  app_data2.set_is_machine_app(true);
  app_data2.set_version(version_app2);
  ProductData product2(app_data2);
  products.push_back(product2);

  UpdateResponses responses;

  UpdateResponseData resp_data1;
  resp_data1.set_guid(app1_guid_);
  resp_data1.set_needs_admin(NEEDS_ADMIN_YES);
  resp_data1.set_status(kResponseStatusOkValue);
  UpdateResponse resp1(resp_data1);
  responses.insert(std::pair<GUID, UpdateResponse>(app1_guid_, resp1));

  UpdateResponseData resp_data2;
  resp_data2.set_guid(app2_guid_);
  resp_data2.set_needs_admin(NEEDS_ADMIN_YES);
  resp_data2.set_status(kResponseStatusOkValue);
  // TODO(omaha): Add component responses here.
  UpdateResponse resp2(resp_data2);
  responses.insert(std::pair<GUID, UpdateResponse>(app2_guid_, resp2));

  Jobs jobs;
  Request ping_request(true);
  CString event_log_text;
  CompletionInfo completion_info;
  EXPECT_SUCCEEDED(job_creator.CreateJobsFromResponses(responses,
                                                       products,
                                                       &jobs,
                                                       &ping_request,
                                                       &event_log_text,
                                                       &completion_info));

  ASSERT_EQ(2, jobs.size());
  EXPECT_EQ(2, ping_request.get_request_count());
  EXPECT_EQ(COMPLETION_SUCCESS, completion_info.status);
  EXPECT_EQ(0, completion_info.error_code);
  EXPECT_TRUE(completion_info.text.IsEmpty());
  EXPECT_FALSE(jobs[0]->is_offline());
  EXPECT_FALSE(jobs[1]->is_offline());

  // Sleep so that there would be a time difference between the time written in
  // the registry and now.
  ::Sleep(20);

  // There should not be any data because this is an install.
  DWORD update_responses(1);
  DWORD64 time_since_first_response_ms(1);
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       app1_guid_,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(0, update_responses);
  EXPECT_EQ(0, time_since_first_response_ms);

  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       app2_guid_,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(0, update_responses);
  EXPECT_EQ(0, time_since_first_response_ms);

  EXPECT_FALSE(RegKey::HasValue(kApp1ClientStateKeyPathMachine,
                                kRegValueLastSuccessfulCheckSec));
  EXPECT_FALSE(RegKey::HasValue(kApp1ClientStateKeyPathMachine,
                                kRegValueLastUpdateTimeSec));
  EXPECT_FALSE(RegKey::HasValue(kApp2ClientStateKeyPathMachine,
                                kRegValueLastSuccessfulCheckSec));
  EXPECT_FALSE(RegKey::HasValue(kApp2ClientStateKeyPathMachine,
                                kRegValueLastUpdateTimeSec));
}

TEST_F(JobCreatorTest, CreateJobsFromResponses_UpdateGoopdateUpdateAvailable) {
  const CString version_app1 = _T("1.1.2.3");
  const CString version_goopdate = _T("1.2.75.3");

  JobCreator job_creator(true, true, &ping_);
  job_creator.set_is_auto_update(true);
  AppManager app_manager(true);

  EXPECT_SUCCEEDED(RegKey::CreateKey(kApp1ClientStateKeyPathMachine));
  EXPECT_SUCCEEDED(RegKey::CreateKey(MACHINE_REG_CLIENT_STATE_GOOPDATE));
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_CLIENTS_GOOPDATE,
                                    _T("pv"),
                                    version_goopdate));

  ProductDataVector products;

  AppData app_data1;
  app_data1.set_app_guid(app1_guid_);
  app_data1.set_is_machine_app(true);
  app_data1.set_version(version_app1);
  ProductData product1(app_data1);
  products.push_back(product1);

  // Add in Goopdate as one of the products.
  AppData app_data2;
  app_data2.set_app_guid(kGoopdateGuid);
  app_data2.set_is_machine_app(true);
  app_data2.set_version(version_goopdate);
  ProductData product2(app_data2);
  products.push_back(product2);
  UpdateResponses responses;

  // Have the first app have an update available, but it should get deferred
  // later since there's a Goopdate update available.
  UpdateResponseData resp_data1;
  resp_data1.set_guid(app1_guid_);
  resp_data1.set_needs_admin(NEEDS_ADMIN_YES);
  resp_data1.set_status(kResponseStatusOkValue);
  UpdateResponse resp1(resp_data1);
  responses.insert(std::pair<GUID, UpdateResponse>(app1_guid_, resp1));

  // Add in response for Goopdate, making it available.
  UpdateResponseData resp_data2;
  resp_data2.set_guid(kGoopdateGuid);
  resp_data2.set_needs_admin(NEEDS_ADMIN_YES);
  resp_data2.set_status(kResponseStatusOkValue);
  // TODO(omaha): Add component responses here.
  UpdateResponse resp2(resp_data2);
  responses.insert(std::pair<GUID, UpdateResponse>(kGoopdateGuid, resp2));

  Jobs jobs;
  Request ping_request(true);
  CString event_log_text;
  CompletionInfo completion_info;

  EXPECT_SUCCEEDED(job_creator.CreateJobsFromResponses(responses,
                                                       products,
                                                       &jobs,
                                                       &ping_request,
                                                       &event_log_text,
                                                       &completion_info));

  // Should be a job for Resp2 since Resp2 was for Goopdate.
  ASSERT_EQ(1, jobs.size());
  EXPECT_TRUE(::IsEqualGUID(jobs[0]->app_data().app_guid(), kGoopdateGuid));
  EXPECT_FALSE(jobs[0]->is_offline());

  // Validate the ping data that is produced by the test method.
  EXPECT_EQ(2, ping_request.get_request_count());
  AppRequestData goopdate_request;
  AppRequestData other_request;

  AppRequestVector::const_iterator iter = ping_request.app_requests_begin();
  const AppRequest& app_request = *iter;
  if (::IsEqualGUID(app_request.request_data().app_data().app_guid(),
                    kGoopdateGuid)) {
    goopdate_request = app_request.request_data();
    other_request = (*++iter).request_data();
  } else {
    goopdate_request = (*++iter).request_data();
    other_request = app_request.request_data();
  }

  EXPECT_EQ(1, goopdate_request.num_ping_events());
  EXPECT_EQ(PingEvent::EVENT_UPDATE_APPLICATION_BEGIN,
            (*goopdate_request.ping_events_begin()).event_type());
  EXPECT_EQ(PingEvent::EVENT_RESULT_SUCCESS,
            (*goopdate_request.ping_events_begin()).event_result());

  EXPECT_EQ(1, other_request.num_ping_events());
  EXPECT_EQ(PingEvent::EVENT_UPDATE_COMPLETE,
            (*other_request.ping_events_begin()).event_type());
  EXPECT_EQ(PingEvent::EVENT_RESULT_UPDATE_DEFERRED,
            (*other_request.ping_events_begin()).event_result());
  EXPECT_EQ(0, (*other_request.ping_events_begin()).error_code());
  EXPECT_EQ(0, (*other_request.ping_events_begin()).extra_code1());

  // Validate the completion info generated by the test method.
  EXPECT_EQ(COMPLETION_SUCCESS, completion_info.status);
  EXPECT_EQ(0, completion_info.error_code);
  EXPECT_TRUE(completion_info.text.IsEmpty());

  EXPECT_EQ(1, metric_worker_skipped_app_update_for_self_update.value());

  // Sleep so that there is a time difference between the time written in the
  // registry and now.
  ::Sleep(20);

  // Stats for app1 should not have been set because we did not process it.
  DWORD update_responses(0);
  DWORD64 time_since_first_response_ms(0);
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       app1_guid_,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(0, update_responses);
  EXPECT_EQ(0, time_since_first_response_ms);

  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       kGoopdateGuid,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(1, update_responses);
  EXPECT_LT(0, time_since_first_response_ms);
  EXPECT_GT(10 * kMsPerSec, time_since_first_response_ms);

  EXPECT_FALSE(RegKey::HasValue(kApp1ClientStateKeyPathMachine,
                                kRegValueLastSuccessfulCheckSec));
  EXPECT_FALSE(RegKey::HasValue(kApp1ClientStateKeyPathMachine,
                                kRegValueLastUpdateTimeSec));
  EXPECT_FALSE(RegKey::HasValue(MACHINE_REG_CLIENT_STATE_GOOPDATE,
                                kRegValueLastSuccessfulCheckSec));
  EXPECT_FALSE(RegKey::HasValue(MACHINE_REG_CLIENT_STATE_GOOPDATE,
                                kRegValueLastUpdateTimeSec));
}

// Tests that in the case of an update for GoogleUpdate only, there is no
// ping sent on behalf of other applications that had no update in the
// first place.
TEST_F(JobCreatorTest, CreateJobsFromResponses_UpdateGoopdateUpdateOnly) {
  const CString version_app1 = _T("1.1.2.3");
  const CString version_goopdate = _T("1.2.75.3");

  JobCreator job_creator(true, true, &ping_);
  job_creator.set_is_auto_update(true);
  AppManager app_manager(true);

  EXPECT_SUCCEEDED(RegKey::CreateKey(kApp1ClientStateKeyPathMachine));
  EXPECT_SUCCEEDED(RegKey::CreateKey(MACHINE_REG_CLIENT_STATE_GOOPDATE));
  EXPECT_SUCCEEDED(RegKey::SetValue(MACHINE_REG_CLIENTS_GOOPDATE,
                                    _T("pv"),
                                    version_goopdate));

  ProductDataVector products;

  AppData app_data1;
  app_data1.set_app_guid(app1_guid_);
  app_data1.set_is_machine_app(true);
  app_data1.set_version(version_app1);
  ProductData product1(app_data1);
  products.push_back(product1);

  // Add in Goopdate as one of the products.
  AppData app_data2;
  app_data2.set_app_guid(kGoopdateGuid);
  app_data2.set_is_machine_app(true);
  app_data2.set_version(version_goopdate);
  ProductData product2(app_data2);
  products.push_back(product2);
  UpdateResponses responses;

  // Have the first app have no update available, but it should get ignored
  // later since there's a Goopdate update available.
  UpdateResponseData resp_data1;
  resp_data1.set_guid(app1_guid_);
  resp_data1.set_needs_admin(NEEDS_ADMIN_YES);
  resp_data1.set_status(kResponseStatusNoUpdate);
  UpdateResponse resp1(resp_data1);
  responses.insert(std::pair<GUID, UpdateResponse>(app1_guid_, resp1));

  // Add in response for Goopdate, making it available.
  UpdateResponseData resp_data2;
  resp_data2.set_guid(kGoopdateGuid);
  resp_data2.set_needs_admin(NEEDS_ADMIN_YES);
  resp_data2.set_status(kResponseStatusOkValue);
  UpdateResponse resp2(resp_data2);
  responses.insert(std::pair<GUID, UpdateResponse>(kGoopdateGuid, resp2));

  Jobs jobs;
  Request ping_request(true);
  CString event_log_text;
  CompletionInfo completion_info;

  EXPECT_SUCCEEDED(job_creator.CreateJobsFromResponses(responses,
                                                       products,
                                                       &jobs,
                                                       &ping_request,
                                                       &event_log_text,
                                                       &completion_info));

  // Should be a job for Resp2 since Resp2 was for Goopdate.
  ASSERT_EQ(1, jobs.size());
  EXPECT_TRUE(::IsEqualGUID(jobs[0]->app_data().app_guid(), kGoopdateGuid));
  EXPECT_FALSE(jobs[0]->is_offline());

  // Validate the ping data that is produced by the test method.
  EXPECT_EQ(1, ping_request.get_request_count());

  AppRequestVector::const_iterator iter = ping_request.app_requests_begin();
  const AppRequest& app_request = *iter;
  AppRequestData goopdate_request = app_request.request_data();

  EXPECT_EQ(1, goopdate_request.num_ping_events());
  EXPECT_EQ(PingEvent::EVENT_UPDATE_APPLICATION_BEGIN,
            (*goopdate_request.ping_events_begin()).event_type());
  EXPECT_EQ(PingEvent::EVENT_RESULT_SUCCESS,
            (*goopdate_request.ping_events_begin()).event_result());

  // Validate the completion info generated by the test method.
  EXPECT_EQ(COMPLETION_SUCCESS, completion_info.status);
  EXPECT_EQ(0, completion_info.error_code);
  EXPECT_TRUE(completion_info.text.IsEmpty());

  EXPECT_EQ(0, metric_worker_skipped_app_update_for_self_update.value());

  // Sleep so that there is a time difference between the time written in the
  // registry and now.
  ::Sleep(20);

  // Stats for app1 should not have been set because we did not process it.
  DWORD update_responses(0);
  DWORD64 time_since_first_response_ms(0);
  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       app1_guid_,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(0, update_responses);
  EXPECT_EQ(0, time_since_first_response_ms);

  app_manager.ReadUpdateAvailableStats(GUID_NULL,
                                       kGoopdateGuid,
                                       &update_responses,
                                       &time_since_first_response_ms);
  EXPECT_EQ(1, update_responses);
  EXPECT_LT(0, time_since_first_response_ms);
  EXPECT_GT(10 * kMsPerSec, time_since_first_response_ms);

  // kRegValueLastSuccessfulCheckSec is not set for app1's "noupdate" response
  // because it is not processed due to the Goopdate update available.
  EXPECT_FALSE(RegKey::HasValue(kApp1ClientStateKeyPathMachine,
                                kRegValueLastSuccessfulCheckSec));
  EXPECT_FALSE(RegKey::HasValue(kApp1ClientStateKeyPathMachine,
                                kRegValueLastUpdateTimeSec));
  EXPECT_FALSE(RegKey::HasValue(MACHINE_REG_CLIENT_STATE_GOOPDATE,
                                kRegValueLastSuccessfulCheckSec));
  EXPECT_FALSE(RegKey::HasValue(MACHINE_REG_CLIENT_STATE_GOOPDATE,
                                kRegValueLastUpdateTimeSec));
}

TEST_F(JobCreatorTest, UpdateResponseDataToCompletionInfo_Ok) {
  UpdateResponseData response_data;
  response_data.set_status(_T("ok"));
  CompletionInfo info = UpdateResponseDataToCompletionInfo(response_data,
                                                           _T("foo"));
  EXPECT_EQ(COMPLETION_SUCCESS, info.status);
  EXPECT_EQ(0, info.error_code);
  EXPECT_TRUE(info.text.IsEmpty());
}

TEST_F(JobCreatorTest, UpdateResponseDataToCompletionInfo_NoUpdate) {
  UpdateResponseData response_data;
  response_data.set_status(_T("NoUpDaTe"));
  CompletionInfo info = UpdateResponseDataToCompletionInfo(response_data,
                                                           _T("foo"));
  EXPECT_EQ(COMPLETION_ERROR, info.status);
  EXPECT_EQ(GOOPDATE_E_NO_UPDATE_RESPONSE, info.error_code);
  EXPECT_STREQ(_T("No update is available."), info.text);
}

TEST_F(JobCreatorTest, UpdateResponseDataToCompletionInfo_Restricted) {
  UpdateResponseData response_data;
  response_data.set_status(_T("ReStRiCtEd"));
  CompletionInfo info = UpdateResponseDataToCompletionInfo(response_data,
                                                           _T("foo"));
  EXPECT_EQ(COMPLETION_ERROR, info.status);
  EXPECT_EQ(GOOPDATE_E_RESTRICTED_SERVER_RESPONSE, info.error_code);
  EXPECT_STREQ(_T("Access to this application is restricted."), info.text);
}

TEST_F(JobCreatorTest,
       UpdateResponseDataToCompletionInfo_OsNotSupported) {
  UpdateResponseData response_data;
  response_data.set_guid(
      StringToGuid(_T("{563CEB0C-A031-4f77-925D-590B2095DE8D}")));
  response_data.set_status(_T("ErRoR-OsNoTsUpPoRtEd"));
  response_data.set_error_url(
      _T("http://foo.google.com/support/article.py?id=12345"));
  CompletionInfo info = UpdateResponseDataToCompletionInfo(response_data,
                                                           _T("My App"));
  EXPECT_EQ(COMPLETION_ERROR, info.status);
  EXPECT_EQ(GOOPDATE_E_OS_NOT_SUPPORTED, info.error_code);
  EXPECT_STREQ(_T("My App does not support your version of Windows. ")
               _T("<a=http://foo.google.com/support/article.py?id=12345>")
               _T("Click here for additional information.</a>"), info.text);
}

TEST_F(JobCreatorTest,
       UpdateResponseDataToCompletionInfo_OsNotSupported_NoOsUrl) {
  UpdateResponseData response_data;
  response_data.set_status(_T("ErRoR-OsNoTsUpPoRtEd"));
  CompletionInfo info = UpdateResponseDataToCompletionInfo(response_data,
                                                           _T("My App"));
  EXPECT_EQ(COMPLETION_ERROR, info.status);
  EXPECT_EQ(GOOPDATE_E_OS_NOT_SUPPORTED, info.error_code);
  EXPECT_STREQ(_T("Server returned the following error: ErRoR-OsNoTsUpPoRtEd. ")
               _T("Please try again later."), info.text);
}

TEST_F(JobCreatorTest, UpdateResponseDataToCompletionInfo_UnknownApp) {
  UpdateResponseData response_data;
  response_data.set_status(_T("eRrOr-UnKnOwNaPpLiCaTiOn"));
  CompletionInfo info = UpdateResponseDataToCompletionInfo(response_data,
                                                           _T("My App"));
  EXPECT_EQ(COMPLETION_ERROR, info.status);
  EXPECT_EQ(GOOPDATE_E_UNKNOWN_APP_SERVER_RESPONSE, info.error_code);
  EXPECT_STREQ(_T("The installer could not install the requested application ")
               _T("due to a server side error. Please try again later. We ")
               _T("apologize for the inconvenience."), info.text);
}

TEST_F(JobCreatorTest, UpdateResponseDataToCompletionInfo_InternalError) {
  UpdateResponseData response_data;
  response_data.set_status(_T("eRrOr-InTeRnAl"));
  CompletionInfo info = UpdateResponseDataToCompletionInfo(response_data,
                                                           _T("My App"));
  EXPECT_EQ(COMPLETION_ERROR, info.status);
  EXPECT_EQ(GOOPDATE_E_INTERNAL_ERROR_SERVER_RESPONSE, info.error_code);
  EXPECT_STREQ(_T("Server returned the following error: eRrOr-InTeRnAl. ")
               _T("Please try again later."), info.text);
}

TEST_F(JobCreatorTest, UpdateResponseDataToCompletionInfo_UnknownResponse) {
  UpdateResponseData response_data;
  response_data.set_status(_T("unknown error string"));
  CompletionInfo info = UpdateResponseDataToCompletionInfo(response_data,
                                                           _T("My App"));
  EXPECT_EQ(COMPLETION_ERROR, info.status);
  EXPECT_EQ(GOOPDATE_E_UNKNOWN_SERVER_RESPONSE, info.error_code);
  EXPECT_STREQ(_T("Server returned the following error: unknown error string. ")
               _T("Please try again later."), info.text);
}

TEST_F(JobCreatorTest, CreateOfflineJobs_Success) {
  // This test does not require registry hive overrides.
  TearDown();

  JobCreator job_creator(true, false, &ping_);
  job_creator.set_fail_if_update_not_available(true);

  EXPECT_SUCCEEDED(RegKey::CreateKey(kApp1ClientStateKeyPathMachine));

  ProductDataVector products;
  AppData app_data1;
  app_data1.set_app_guid(app1_guid_);
  app_data1.set_display_name(_T("Test App 1"));
  app_data1.set_is_machine_app(true);
  app_data1.set_language(_T("en"));
  ProductData product1(app_data1);
  products.push_back(product1);

  CString offline_manifest_path(kGuidApp1);
  offline_manifest_path += _T(".gup");
  offline_manifest_path = ConcatenatePath(app_util::GetCurrentModuleDirectory(),
                                          offline_manifest_path);
  ASSERT_SUCCEEDED(File::Copy(
      ConcatenatePath(app_util::GetCurrentModuleDirectory(),
                      _T("server_manifest_one_app.xml")),
      offline_manifest_path,
      true));

  CString installer_exe = _T("foo_installer.exe");
  CString installer_path = ConcatenatePath(
                                app_util::GetCurrentModuleDirectory(),
                                kGuidApp1);
  ASSERT_SUCCEEDED(CreateDir(installer_path, NULL));
  // The hash of SaveArguments_OmahaTestSigned.exe needs to be kept in sync, in
  // server_manifest_one_app.xml, for this test to succeed.
  ASSERT_SUCCEEDED(File::Copy(
      ConcatenatePath(ConcatenatePath(app_util::GetCurrentModuleDirectory(),
                                      _T("unittest_support")),
                                      _T("SaveArguments_OmahaTestSigned.exe")),
      ConcatenatePath(installer_path, installer_exe),
      true));

  Jobs jobs;
  Request ping_request(true);
  CString event_log_text;
  CompletionInfo completion_info;
  ASSERT_SUCCEEDED(job_creator.CreateOfflineJobs(
      app_util::GetCurrentModuleDirectory(),
      products,
      &jobs,
      &ping_request,
      &event_log_text,
      &completion_info));

  ASSERT_EQ(1, jobs.size());
  EXPECT_EQ(1, ping_request.get_request_count());
  EXPECT_EQ(COMPLETION_SUCCESS, completion_info.status);
  EXPECT_EQ(0, completion_info.error_code);
  EXPECT_TRUE(completion_info.text.IsEmpty());
  EXPECT_EQ(SUCCESS_ACTION_EXIT_SILENTLY_ON_LAUNCH_CMD,
            jobs[0]->response_data().success_action());
  EXPECT_TRUE(jobs[0]->is_offline());

  EXPECT_FALSE(RegKey::HasValue(kApp1ClientStateKeyPathMachine,
                                kRegValueLastSuccessfulCheckSec));
  EXPECT_FALSE(RegKey::HasValue(kApp1ClientStateKeyPathMachine,
                                kRegValueLastUpdateTimeSec));

  EXPECT_SUCCEEDED(DeleteDirectory(installer_path));
  EXPECT_SUCCEEDED(File::Remove(offline_manifest_path));
}

TEST_F(JobCreatorTest, CreateOfflineJobs_Failure) {
  JobCreator job_creator(true, false, &ping_);
  job_creator.set_fail_if_update_not_available(true);

  EXPECT_SUCCEEDED(RegKey::CreateKey(kApp1ClientStateKeyPathMachine));

  ProductDataVector products;
  AppData app_data1;
  app_data1.set_app_guid(app1_guid_);
  app_data1.set_display_name(_T("Test App 1"));
  app_data1.set_is_machine_app(true);
  app_data1.set_language(_T("en"));
  ProductData product1(app_data1);
  products.push_back(product1);

  Jobs jobs;
  Request ping_request(true);
  CString event_log_text;
  CompletionInfo completion_info;
  ASSERT_EQ(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
            job_creator.CreateOfflineJobs(
                            app_util::GetCurrentModuleDirectory(),
                            products,
                            &jobs,
                            &ping_request,
                            &event_log_text,
                            &completion_info));

  EXPECT_EQ(0, jobs.size());
  EXPECT_EQ(0, ping_request.get_request_count());
  EXPECT_EQ(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
            completion_info.error_code);
  EXPECT_EQ(COMPLETION_ERROR, completion_info.status);
  EXPECT_STREQ(_T(""), completion_info.text);

  EXPECT_FALSE(RegKey::HasValue(kApp1ClientStateKeyPathMachine,
                                kRegValueLastSuccessfulCheckSec));
  EXPECT_FALSE(RegKey::HasValue(kApp1ClientStateKeyPathMachine,
                                kRegValueLastUpdateTimeSec));
}

TEST_F(JobCreatorTest, FindOfflineFilePath_Success) {
  CString installer_exe = _T("foo_installer.exe");
  CString installer_path = ConcatenatePath(
                                app_util::GetCurrentModuleDirectory(),
                                kGuidApp1);
  ASSERT_SUCCEEDED(CreateDir(installer_path, NULL));
  ASSERT_SUCCEEDED(File::Copy(
      ConcatenatePath(app_util::GetCurrentModuleDirectory(),
                      _T("LongRunning.exe")),
      ConcatenatePath(installer_path, installer_exe),
      true));

  CString file_path;
  ASSERT_SUCCEEDED(CallFindOfflineFilePath(
                       app_util::GetCurrentModuleDirectory(),
                       kGuidApp1,
                       &file_path));
  ASSERT_STREQ(ConcatenatePath(installer_path, installer_exe), file_path);

  EXPECT_SUCCEEDED(DeleteDirectory(installer_path));
}

TEST_F(JobCreatorTest, FindOfflineFilePath_Failure) {
  CString file_path;
  ASSERT_EQ(HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND),
            CallFindOfflineFilePath(app_util::GetCurrentModuleDirectory(),
                                    kGuidApp1,
                                    &file_path));
  ASSERT_TRUE(file_path.IsEmpty());
}

TEST_F(JobCreatorTest, ReadOfflineManifest_Success) {
  // This test does not require registry hive overrides.
  TearDown();

  CString offline_manifest_path(kGuidApp1);
  offline_manifest_path += _T(".gup");
  offline_manifest_path = ConcatenatePath(app_util::GetCurrentModuleDirectory(),
                                          offline_manifest_path);
  ASSERT_SUCCEEDED(File::Copy(
      ConcatenatePath(app_util::GetCurrentModuleDirectory(),
                      _T("server_manifest_one_app.xml")),
      offline_manifest_path,
      true));

  UpdateResponse response;
  EXPECT_SUCCEEDED(CallReadOfflineManifest(
                       app_util::GetCurrentModuleDirectory(),
                       kGuidApp1,
                       &response));

  EXPECT_EQ(SUCCESS_ACTION_EXIT_SILENTLY_ON_LAUNCH_CMD,
            response.update_response_data().success_action());

  EXPECT_SUCCEEDED(File::Remove(offline_manifest_path));
}

TEST_F(JobCreatorTest, ReadOfflineManifest_FileDoesNotExist) {
  UpdateResponse response;
  ASSERT_EQ(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
            CallReadOfflineManifest(app_util::GetCurrentModuleDirectory(),
                                    kGuidApp1,
                                    &response));
}

}  // namespace omaha

