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

#include <string.h>
#include "omaha/base/string.h"
#include "omaha/base/logging.h"
#include "omaha/base/omaha_version.h"
#include "omaha/base/utils.h"
#include "omaha/common/command_line.h"
#include "omaha/common/ping.h"
#include "omaha/testing/unit_test.h"

namespace omaha {

class PingTest : public testing::Test {
};

TEST_F(PingTest, BuildOmahaPing) {
  PingEventPtr ping_event1(
      new PingEvent(PingEvent::EVENT_INSTALL_COMPLETE,
                    PingEvent::EVENT_RESULT_SUCCESS,
                    10,
                    20));

  PingEventPtr ping_event2(
      new PingEvent(PingEvent::EVENT_INSTALL_COMPLETE,
                    PingEvent::EVENT_RESULT_SUCCESS,
                    30,
                    40));

  CommandLineExtraArgs command_line_extra_args;
  StringToGuidSafe(_T("{DE06587E-E5AB-4364-A46B-F3AC733007B3}"),
                   &command_line_extra_args.installation_id);
  command_line_extra_args.brand_code = _T("GGLS");
  command_line_extra_args.client_id  = _T("a client id");
  command_line_extra_args.language   = _T("en");

  // Machine ping.
  Ping install_ping(true, _T("session"), _T("oneclick"));
  install_ping.LoadAppDataFromExtraArgs(command_line_extra_args);
  install_ping.BuildOmahaPing(_T("1.0.0.0"),
                              _T("2.0.0.0"),
                              ping_event1,
                              ping_event2);

  CString expected_ping_request_substring;
  expected_ping_request_substring.Format(_T("<app appid=\"{430FD4D0-B729-4F61-AA34-91526481799D}\" version=\"1.0.0.0\" nextversion=\"2.0.0.0\" lang=\"en\" brand=\"GGLS\" client=\"a client id\" iid=\"{DE06587E-E5AB-4364-A46B-F3AC733007B3}\"><event eventtype=\"2\" eventresult=\"1\" errorcode=\"10\" extracode1=\"20\"/><event eventtype=\"2\" eventresult=\"1\" errorcode=\"30\" extracode1=\"40\"/></app>"));  // NOLINT

  CString actual_ping_request;
  install_ping.BuildRequestString(&actual_ping_request);

  // The ping_request_string contains some data that depends on the machine
  // environment, such as operating system version. Look for a partial match in
  // the string corresponding to the <app> element.
  EXPECT_NE(-1, actual_ping_request.Find(expected_ping_request_substring));
}

TEST_F(PingTest, BuildAppsPing) {
  const TCHAR* const kOmahaUserClientStatePath =
      _T("HKCU\\Software\\") SHORT_COMPANY_NAME _T("\\") PRODUCT_NAME
      _T("\\ClientState\\") GOOPDATE_APP_ID;

  const CString expected_pv           = _T("1.3.23.0");
  const CString expected_lang         = _T("en");
  const CString expected_brand_code   = _T("GGLS");
  const CString expected_client_id    = _T("someclientid");
  const CString expected_iid          =
      _T("{7C0B6E56-B24B-436b-A960-A6EA201E886F}");
  const CString expected_experiment_label =
      _T("some_experiment=a|Fri, 14 Aug 2015 16:13:03 GMT");

  EXPECT_HRESULT_SUCCEEDED(RegKey::SetValue(kOmahaUserClientStatePath,
                                            kRegValueProductVersion,
                                            expected_pv));
  EXPECT_HRESULT_SUCCEEDED(RegKey::SetValue(kOmahaUserClientStatePath,
                                            kRegValueLanguage,
                                            expected_lang));
  EXPECT_HRESULT_SUCCEEDED(RegKey::SetValue(kOmahaUserClientStatePath,
                                            kRegValueBrandCode,
                                            expected_brand_code));
  EXPECT_HRESULT_SUCCEEDED(RegKey::SetValue(kOmahaUserClientStatePath,
                                            kRegValueClientId,
                                            expected_client_id));
  EXPECT_HRESULT_SUCCEEDED(RegKey::SetValue(kOmahaUserClientStatePath,
                                            kRegValueInstallationId,
                                            expected_iid));
  EXPECT_HRESULT_SUCCEEDED(RegKey::SetValue(kOmahaUserClientStatePath,
                                            kRegValueExperimentLabels,
                                            expected_experiment_label));

  PingEventPtr ping_event(
      new PingEvent(PingEvent::EVENT_INSTALL_COMPLETE,
                    PingEvent::EVENT_RESULT_SUCCESS,
                    34,
                    6));

  Ping apps_ping(false, _T("unittest"), _T("InstallSource_Foo"));
  std::vector<CString> apps;
  apps.push_back(GOOPDATE_APP_ID);
  apps_ping.LoadAppDataFromRegistry(apps);
  apps_ping.BuildAppsPing(ping_event);

  CString expected_ping_request_substring;
  expected_ping_request_substring.Format(_T("<app appid=\"{430FD4D0-B729-4F61-AA34-91526481799D}\" version=\"1.3.23.0\" nextversion=\"\" lang=\"en\" brand=\"GGLS\" client=\"someclientid\" experiments=\"some_experiment=a|Fri, 14 Aug 2015 16:13:03 GMT\" iid=\"{7C0B6E56-B24B-436b-A960-A6EA201E886F}\"><event eventtype=\"2\" eventresult=\"1\" errorcode=\"34\" extracode1=\"6\"/></app>"));  // NOLINT

  CString actual_ping_request;
  apps_ping.BuildRequestString(&actual_ping_request);

  EXPECT_NE(-1, actual_ping_request.Find(expected_ping_request_substring));
}

TEST_F(PingTest, SendString) {
  CString request_string = _T("<?xml version=\"1.0\" encoding=\"UTF-8\"?><request protocol=\"3.0\" version=\"1.3.23.0\" ismachine=\"1\" sessionid=\"unittest\" installsource=\"oneclick\" testsource=\"dev\" requestid=\"{EC821C33-E4EE-4E75-BC85-7E9DFC3652F5}\" periodoverridesec=\"7407360\"><os platform=\"win\" version=\"6.0\" sp=\"Service Pack 1\"/><app appid=\"{430FD4D0-B729-4F61-AA34-91526481799D}\" version=\"1.0.0.0\" nextversion=\"2.0.0.0\" lang=\"en\" brand=\"GGLS\" client=\"a client id\" iid=\"{DE06587E-E5AB-4364-A46B-F3AC733007B3}\"><event eventtype=\"10\" eventresult=\"1\" errorcode=\"0\" extracode1=\"0\"/></app></request>");   // NOLINT
  EXPECT_HRESULT_SUCCEEDED(Ping::SendString(false,
                                            HeadersVector(),
                                            request_string));

  // 400 Bad Request returned by the server.
  EXPECT_EQ(0x80042190, Ping::SendString(false, HeadersVector(), _T("")));
}

TEST_F(PingTest, HandlePing) {
  CString request_string = _T("<?xml version=\"1.0\" encoding=\"UTF-8\"?><request protocol=\"3.0\" version=\"1.3.23.0\" ismachine=\"1\" sessionid=\"unittest\" installsource=\"oneclick\" testsource=\"dev\" requestid=\"{EC821C33-E4EE-4E75-BC85-7E9DFC3652F5}\" periodoverridesec=\"7407360\"><os platform=\"win\" version=\"6.0\" sp=\"Service Pack 1\"/><app appid=\"{430FD4D0-B729-4F61-AA34-91526481799D}\" version=\"1.0.0.0\" nextversion=\"2.0.0.0\" lang=\"en\" brand=\"GGLS\" client=\"a client id\" iid=\"{DE06587E-E5AB-4364-A46B-F3AC733007B3}\"><event eventtype=\"10\" eventresult=\"1\" errorcode=\"0\" extracode1=\"0\"/></app></request>");   // NOLINT

  CStringA request_string_utf8(WideToUtf8(request_string));
  CStringA ping_string_utf8;
  WebSafeBase64Escape(request_string_utf8, &ping_string_utf8);

  EXPECT_HRESULT_SUCCEEDED(
      Ping::HandlePing(false, Utf8ToWideChar(ping_string_utf8,
                                             ping_string_utf8.GetLength())));

  // 400 Bad Request returned by the server.
  EXPECT_EQ(0x80042190, Ping::HandlePing(false, _T("")));
}

TEST_F(PingTest, SendInProcess) {
  PingEventPtr ping_event(
      new PingEvent(PingEvent::EVENT_INSTALL_COMPLETE,
                    PingEvent::EVENT_RESULT_SUCCESS,
                    0,
                    0));

  CommandLineExtraArgs command_line_extra_args;
  StringToGuidSafe(_T("{DE06587E-E5AB-4364-A46B-F3AC733007B3}"),
                   &command_line_extra_args.installation_id);
  command_line_extra_args.brand_code = _T("GGLS");
  command_line_extra_args.client_id  = _T("a client id");
  command_line_extra_args.language   = _T("en");

  // User ping.
  Ping install_ping(false, _T("unittest"), _T("oneclick"));
  install_ping.LoadAppDataFromExtraArgs(command_line_extra_args);
  install_ping.BuildOmahaPing(_T("1.0.0.0"), _T("2.0.0.0"), ping_event);

  CString request_string;
  EXPECT_HRESULT_SUCCEEDED(install_ping.BuildRequestString(&request_string));
  EXPECT_HRESULT_SUCCEEDED(install_ping.SendInProcess(request_string));
}

TEST_F(PingTest, IsPingExpired_PastTime) {
  const time64 time = GetCurrent100NSTime() - (Ping::kPingExpiry100ns + 1);
  EXPECT_TRUE(Ping::IsPingExpired(time));
}

TEST_F(PingTest, IsPingExpired_CurrentTime) {
  const time64 time = GetCurrent100NSTime();
  EXPECT_FALSE(Ping::IsPingExpired(time));
}

TEST_F(PingTest, IsPingExpired_FutureTime) {
  const time64 time = GetCurrent100NSTime() + 10;
  EXPECT_TRUE(Ping::IsPingExpired(time));
}

TEST_F(PingTest, LoadPersistedPings_NoPersistedPings) {
  Ping::PingsVector pings;
  EXPECT_EQ(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
            Ping::LoadPersistedPings(false, &pings));
  EXPECT_EQ(0, pings.size());
}

TEST_F(PingTest, LoadPersistedPings) {
  CString ping_reg_path(Ping::GetPingRegPath(false));

  EXPECT_HRESULT_SUCCEEDED(RegKey::SetValue(ping_reg_path,
                                            _T("1"),
                                            _T("Test Ping String 1")));
  EXPECT_HRESULT_SUCCEEDED(RegKey::SetValue(ping_reg_path,
                                            _T("2"),
                                            _T("Test Ping String 2")));
  EXPECT_HRESULT_SUCCEEDED(RegKey::SetValue(ping_reg_path,
                                            _T("3"),
                                            _T("Test Ping String 3")));

  Ping::PingsVector pings;
  EXPECT_HRESULT_SUCCEEDED(Ping::LoadPersistedPings(false, &pings));
  EXPECT_EQ(3, pings.size());

  EXPECT_EQ(1, pings[0].first);
  EXPECT_EQ(2, pings[1].first);
  EXPECT_EQ(3, pings[2].first);
  EXPECT_STREQ(_T("Test Ping String 1"), pings[0].second);
  EXPECT_STREQ(_T("Test Ping String 2"), pings[1].second);
  EXPECT_STREQ(_T("Test Ping String 3"), pings[2].second);

  EXPECT_HRESULT_SUCCEEDED(RegKey::DeleteKey(ping_reg_path));
}

TEST_F(PingTest, PersistPing) {
  EXPECT_HRESULT_SUCCEEDED(Ping::PersistPing(false, _T("Test Ping String 1")));
  ::Sleep(15);
  EXPECT_HRESULT_SUCCEEDED(Ping::PersistPing(false, _T("Test Ping String 2")));
  ::Sleep(15);
  EXPECT_HRESULT_SUCCEEDED(Ping::PersistPing(false, _T("Test Ping String 3")));

  Ping::PingsVector pings;
  EXPECT_HRESULT_SUCCEEDED(Ping::LoadPersistedPings(false, &pings));
  EXPECT_EQ(3, pings.size());

  EXPECT_FALSE(Ping::IsPingExpired(pings[0].first));
  EXPECT_FALSE(Ping::IsPingExpired(pings[1].first));
  EXPECT_FALSE(Ping::IsPingExpired(pings[2].first));
  EXPECT_STREQ(_T("Test Ping String 1"), pings[0].second);
  EXPECT_STREQ(_T("Test Ping String 2"), pings[1].second);
  EXPECT_STREQ(_T("Test Ping String 3"), pings[2].second);

  EXPECT_HRESULT_SUCCEEDED(RegKey::DeleteKey(Ping::GetPingRegPath(false)));
}

TEST_F(PingTest, DeletePersistedPing) {
  CString ping_reg_path(Ping::GetPingRegPath(false));

  EXPECT_HRESULT_SUCCEEDED(RegKey::SetValue(ping_reg_path,
                                            _T("1"),
                                            _T("Test Ping String 1")));
  EXPECT_HRESULT_SUCCEEDED(RegKey::SetValue(ping_reg_path,
                                            _T("2"),
                                            _T("Test Ping String 2")));

  EXPECT_HRESULT_SUCCEEDED(Ping::DeletePersistedPing(false, 1));
  EXPECT_HRESULT_SUCCEEDED(Ping::DeletePersistedPing(false, 2));

  EXPECT_FALSE(RegKey::HasKey(ping_reg_path));
}

TEST_F(PingTest, SendPersistedPings) {
  PingEventPtr ping_event(
      new PingEvent(PingEvent::EVENT_INSTALL_COMPLETE,
                    PingEvent::EVENT_RESULT_SUCCESS,
                    0,
                    0));

  CommandLineExtraArgs command_line_extra_args;
  StringToGuidSafe(_T("{DE06587E-E5AB-4364-A46B-F3AC733007B3}"),
                   &command_line_extra_args.installation_id);
  command_line_extra_args.brand_code = _T("GGLS");
  command_line_extra_args.client_id  = _T("a client id");
  command_line_extra_args.language   = _T("en");

  // User ping.
  Ping install_ping(false, _T("unittest"), _T("oneclick"));
  install_ping.LoadAppDataFromExtraArgs(command_line_extra_args);
  install_ping.BuildOmahaPing(_T("1.0.0.0"), _T("2.0.0.0"), ping_event);

  CString request_string;
  EXPECT_HRESULT_SUCCEEDED(install_ping.BuildRequestString(&request_string));
  EXPECT_HRESULT_SUCCEEDED(Ping::PersistPing(false, request_string));

  EXPECT_HRESULT_SUCCEEDED(Ping::SendPersistedPings(false));

  EXPECT_FALSE(RegKey::HasKey(Ping::GetPingRegPath(false)));
}

// The tests below rely on the out-of-process mechanism to send install pings.
// Enable the test to debug the sending code.
TEST_F(PingTest, DISABLED_SendUsingGoogleUpdate) {
  PingEventPtr ping_event(
      new PingEvent(PingEvent::EVENT_INSTALL_COMPLETE,
                    PingEvent::EVENT_RESULT_SUCCESS,
                    0,
                    0));

  CommandLineExtraArgs command_line_extra_args;
  StringToGuidSafe(_T("{DE06587E-E5AB-4364-A46B-F3AC733007B3}"),
                   &command_line_extra_args.installation_id);
  command_line_extra_args.brand_code = _T("GGLS");
  command_line_extra_args.client_id  = _T("a client id");
  command_line_extra_args.language   = _T("en");

  // User ping and wait for completion.
  Ping install_ping(false, _T("unittest"), _T("oneclick"));
  install_ping.LoadAppDataFromExtraArgs(command_line_extra_args);
  install_ping.BuildOmahaPing(_T("1.0.0.0"), _T("2.0.0.0"), ping_event);

  const int kWaitForPingProcessToCompleteMs = 60000;
  CString request_string;
  EXPECT_HRESULT_SUCCEEDED(install_ping.BuildRequestString(&request_string));
  EXPECT_HRESULT_SUCCEEDED(install_ping.SendUsingGoogleUpdate(
      request_string, kWaitForPingProcessToCompleteMs));
}

TEST_F(PingTest, Send_Empty) {
  CommandLineExtraArgs command_line_extra_args;
  Ping install_ping(false, _T("unittest"), _T("oneclick"));
  EXPECT_EQ(S_FALSE, install_ping.Send(false));
}

TEST_F(PingTest, DISABLED_Send) {
  PingEventPtr ping_event(
      new PingEvent(PingEvent::EVENT_INSTALL_COMPLETE,
                    PingEvent::EVENT_RESULT_SUCCESS,
                    0,
                    0));

  CommandLineExtraArgs command_line_extra_args;
  StringToGuidSafe(_T("{DE06587E-E5AB-4364-A46B-F3AC733007B3}"),
                   &command_line_extra_args.installation_id);
  command_line_extra_args.brand_code = _T("GGLS");
  command_line_extra_args.client_id  = _T("a client id");
  command_line_extra_args.language   = _T("en");

  // User ping and wait for completion.
  Ping install_ping(false, _T("unittest"), _T("oneclick"));
  install_ping.LoadAppDataFromExtraArgs(command_line_extra_args);
  install_ping.BuildOmahaPing(_T("1.0.0.0"), _T("2.0.0.0"), ping_event);

  EXPECT_HRESULT_SUCCEEDED(install_ping.Send(false));
}

TEST_F(PingTest, DISABLED_SendFireAndForget) {
  PingEventPtr ping_event(
      new PingEvent(PingEvent::EVENT_INSTALL_COMPLETE,
                    PingEvent::EVENT_RESULT_SUCCESS,
                    0,
                    0));

  CommandLineExtraArgs command_line_extra_args;
  StringToGuidSafe(_T("{DE06587E-E5AB-4364-A46B-F3AC733007B3}"),
                   &command_line_extra_args.installation_id);
  command_line_extra_args.brand_code = _T("GGLS");
  command_line_extra_args.client_id  = _T("a client id");
  command_line_extra_args.language   = _T("en");

  // User ping and do not wait for completion.
  Ping install_ping(false, _T("unittest"), _T("oneclick"));
  install_ping.LoadAppDataFromExtraArgs(command_line_extra_args);
  install_ping.BuildOmahaPing(_T("1.0.0.0"), _T("2.0.0.0"), ping_event);

  EXPECT_HRESULT_SUCCEEDED(install_ping.Send(true));
}

}  // namespace omaha

