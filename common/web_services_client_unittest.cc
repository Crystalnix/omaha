// Copyright 2008-2010 Google Inc.
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

#include "base/scoped_ptr.h"
#include "omaha/base/const_addresses.h"
#include "omaha/base/scoped_any.h"
#include "omaha/base/scoped_ptr_address.h"
#include "omaha/base/string.h"
#include "omaha/base/vista_utils.h"
#include "omaha/common/config_manager.h"
#include "omaha/common/update_request.h"
#include "omaha/common/update_response.h"
#include "omaha/common/web_services_client.h"
#include "omaha/net/network_request.h"
#include "omaha/testing/unit_test.h"

using ::testing::_;

namespace omaha {

// TODO(omaha): test the machine case.

class WebServicesClientTest : public testing::Test {
 protected:
  virtual void SetUp() {
    EXPECT_HRESULT_SUCCEEDED(
        ConfigManager::Instance()->GetUpdateCheckUrl(&update_check_url_));

    web_service_client_.reset(new WebServicesClient(false));

    update_request_.reset(xml::UpdateRequest::Create(false,
                                                     _T("unittest_sessionid"),
                                                     _T("unittest_instsource"),
                                                     CString()));
    update_response_.reset(xml::UpdateResponse::Create());
  }

  virtual void TearDown() {
    web_service_client_.reset();
  }

  NetworkRequest* network_request() const {
    return web_service_client_->network_request();
  }

  CString update_check_url_;

  scoped_ptr<WebServicesClient> web_service_client_;
  scoped_ptr<xml::UpdateRequest> update_request_;
  scoped_ptr<xml::UpdateResponse> update_response_;
};

TEST_F(WebServicesClientTest, Send) {
  EXPECT_HRESULT_SUCCEEDED(web_service_client_->Initialize(update_check_url_,
                                                           HeadersVector(),
                                                           false));

  // Test sending a user update check request.
  EXPECT_HRESULT_SUCCEEDED(web_service_client_->Send(update_request_.get(),
                                                     update_response_.get()));
  EXPECT_TRUE(web_service_client_->is_http_success());

  xml::response::Response response(update_response_->response());
  EXPECT_STREQ(_T("3.0"), response.protocol);

  NetworkRequest* network_request(network_request());

  CString cookie;
  EXPECT_HRESULT_FAILED(network_request->QueryHeadersString(
      WINHTTP_QUERY_FLAG_REQUEST_HEADERS | WINHTTP_QUERY_COOKIE,
      WINHTTP_HEADER_NAME_BY_INDEX,
      &cookie));
  EXPECT_TRUE(cookie.IsEmpty());

  CString etag;
  EXPECT_HRESULT_FAILED(network_request->QueryHeadersString(
      WINHTTP_QUERY_ETAG, WINHTTP_HEADER_NAME_BY_INDEX, &etag));
  EXPECT_TRUE(etag.IsEmpty());
}

TEST_F(WebServicesClientTest, SendUsingCup) {
  EXPECT_HRESULT_SUCCEEDED(web_service_client_->Initialize(update_check_url_,
                                                           HeadersVector(),
                                                           true));

  // Test sending a user update check request.
  EXPECT_HRESULT_SUCCEEDED(web_service_client_->Send(update_request_.get(),
                                                     update_response_.get()));
  EXPECT_TRUE(web_service_client_->is_http_success());

  xml::response::Response response(update_response_->response());
  EXPECT_STREQ(_T("3.0"), response.protocol);

  NetworkRequest* network_request(network_request());

  CString no_request_age_header;
  network_request->QueryHeadersString(
      WINHTTP_QUERY_CUSTOM | WINHTTP_QUERY_FLAG_REQUEST_HEADERS,
      _T("X-RequestAge"),
      &no_request_age_header);

  EXPECT_STREQ(_T(""), no_request_age_header);

  // A CUP transaction has either a request or a response CUP cookie and
  // the ETag response header.
  CString request_cookie;
  network_request->QueryHeadersString(
      WINHTTP_QUERY_COOKIE | WINHTTP_QUERY_FLAG_REQUEST_HEADERS,
      WINHTTP_HEADER_NAME_BY_INDEX,
      &request_cookie);
  const bool has_cup_request_cookie = request_cookie.Find(_T("c=")) != -1;

  CString response_cookie;
  network_request->QueryHeadersString(WINHTTP_QUERY_SET_COOKIE,
                                      WINHTTP_HEADER_NAME_BY_INDEX,
                                      &response_cookie);
  const bool has_cup_response_cookie = response_cookie.Find(_T("c=")) != -1;

  EXPECT_TRUE(has_cup_request_cookie || has_cup_response_cookie);

  CString etag;
  EXPECT_HRESULT_SUCCEEDED(network_request->QueryHeadersString(
      WINHTTP_QUERY_ETAG, WINHTTP_HEADER_NAME_BY_INDEX, &etag));
  EXPECT_FALSE(etag.IsEmpty());
}

TEST_F(WebServicesClientTest, SendForcingHttps) {
  // Skips the test if the update check URL is not https.
  if (!String_StartsWith(update_check_url_, kHttpsProtoScheme, true)) {
    return;
  }

  EXPECT_HRESULT_SUCCEEDED(web_service_client_->Initialize(update_check_url_,
                                                           HeadersVector(),
                                                           true));

  EXPECT_TRUE(update_request_->IsEmpty());

  // Adds an application with non-empty tt_token to the update request.
  // This should prevent the network stack from replacing https with
  // CUP protocol.
  xml::request::App app;
  app.app_id = _T("{21CD0965-0B0E-47cf-B421-2D191C16C0E2}");
  app.iid    = _T("{00000000-0000-0000-0000-000000000000}");
  app.update_check.is_valid = true;
  app.update_check.tt_token = _T("Test TT token");
  update_request_->AddApp(app);

  EXPECT_FALSE(update_request_->IsEmpty());
  EXPECT_TRUE(update_request_->has_tt_token());

  EXPECT_HRESULT_SUCCEEDED(web_service_client_->Send(update_request_.get(),
                                                    update_response_.get()));
  EXPECT_TRUE(web_service_client_->is_http_success());

  // Do a couple of sanity checks on the parsing of the response.
  xml::response::Response response(update_response_->response());
  EXPECT_STREQ(_T("3.0"), response.protocol);
  ASSERT_EQ(1, response.apps.size());
  EXPECT_STREQ(_T("error-unknownApplication"), response.apps[0].status);
}

TEST_F(WebServicesClientTest, SendWithCustomHeader) {
  HeadersVector headers;
  headers.push_back(std::make_pair(_T("X-RequestAge"), _T("200")));

  EXPECT_HRESULT_SUCCEEDED(web_service_client_->Initialize(update_check_url_,
                                                           headers,
                                                           true));

  EXPECT_HRESULT_SUCCEEDED(web_service_client_->Send(update_request_.get(),
                                                     update_response_.get()));
  EXPECT_TRUE(web_service_client_->is_http_success());

  xml::response::Response response(update_response_->response());
  EXPECT_STREQ(_T("3.0"), response.protocol);

  NetworkRequest* network_request(network_request());

  CString request_age_header;
  network_request->QueryHeadersString(
      WINHTTP_QUERY_CUSTOM | WINHTTP_QUERY_FLAG_REQUEST_HEADERS,
      _T("X-RequestAge"),
      &request_age_header);

  EXPECT_STREQ(_T("200"), request_age_header);
}

TEST_F(WebServicesClientTest, SendString) {
  EXPECT_HRESULT_SUCCEEDED(web_service_client_->Initialize(update_check_url_,
                                                           HeadersVector(),
                                                           false));

  // Test sending a user update check request.
  CString request_string =
    _T("<?xml version=\"1.0\" encoding=\"UTF-8\"?>")
    _T("<request protocol=\"3.0\" testsource=\"dev\"></request>");
  scoped_ptr<xml::UpdateResponse> response(xml::UpdateResponse::Create());
  EXPECT_HRESULT_SUCCEEDED(web_service_client_->SendString(&request_string,
                                                           response.get()));
  EXPECT_TRUE(web_service_client_->is_http_success());
}

TEST_F(WebServicesClientTest, SendStringWithCustomHeader) {
  HeadersVector headers;
  headers.push_back(std::make_pair(_T("X-FooBar"), _T("424")));

  EXPECT_HRESULT_SUCCEEDED(web_service_client_->Initialize(update_check_url_,
                                                           headers,
                                                           false));

  // Test sending a user update check request.
  CString request_string =
    _T("<?xml version=\"1.0\" encoding=\"UTF-8\"?>")
    _T("<request protocol=\"3.0\" testsource=\"dev\"></request>");
  scoped_ptr<xml::UpdateResponse> response(xml::UpdateResponse::Create());
  EXPECT_HRESULT_SUCCEEDED(web_service_client_->SendString(&request_string,
                                                           response.get()));
  EXPECT_TRUE(web_service_client_->is_http_success());

  NetworkRequest* network_request(network_request());

  CString foobar_header;
  network_request->QueryHeadersString(
      WINHTTP_QUERY_CUSTOM | WINHTTP_QUERY_FLAG_REQUEST_HEADERS,
      _T("X-FooBar"),
      &foobar_header);

  EXPECT_STREQ(_T("424"), foobar_header);
}

}  // namespace omaha

