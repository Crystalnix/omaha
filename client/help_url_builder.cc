// Copyright 2008 Google Inc.
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
#include "omaha/client/help_url_builder.h"
#include <windows.h>
#include <atlstr.h>
#include <vector>
#include "omaha/base/debug.h"
#include "omaha/base/logging.h"
#include "omaha/base/omaha_version.h"
#include "omaha/base/string.h"
#include "omaha/base/utils.h"
#include "omaha/base/vistautil.h"
#include "omaha/common/config_manager.h"
#include "omaha/common/goopdate_utils.h"
#include "goopdate/omaha3_idl.h"
#include "omaha/net/http_client.h"

namespace omaha {

namespace {

// Query element name-value pair.
typedef std::pair<CString, CString> QueryElement;

// Builds a query string from the provided name-value pairs.
// The string does not begin or end in a pair separator.
HRESULT BuildQueryString(const std::vector<QueryElement>& elements,
                         CString* query) {
  ASSERT1(query);

  query->Empty();

  for (size_t i = 0; i < elements.size(); ++i) {
    CString escaped_str;
    HRESULT hr = StringEscape(elements[i].second, false, &escaped_str);
    if (FAILED(hr)) {
      CORE_LOG(LEVEL_WARNING, (_T("[StringEscape failed][0x%08x]"), hr));
      return hr;
    }

    CString element;
    element.FormatMessage(_T("%1=%2"), elements[i].first, escaped_str);

    if (0 < i) {
      query->Append(_T("&"));
    }
    query->Append(element);
  }

  return S_OK;
}

}   // namespace

HRESULT HelpUrlBuilder::BuildUrl(const std::vector<AppResult>& app_results,
                                 CString* help_url) const {
  ASSERT1(help_url);
  help_url->Empty();

  CString more_info_url;
  VERIFY1(SUCCEEDED(ConfigManager::Instance()->GetMoreInfoUrl(&more_info_url)));

  const TCHAR* const kHelpLinkSourceId = _T("gethelp");
  HRESULT hr = BuildHttpGetString(more_info_url,
                                  app_results,
                                  GetVersionString(),
                                  kHelpLinkSourceId,
                                  help_url);
  if (FAILED(hr)) {
    // Make sure a failed URL is not displayed.
    help_url->Empty();
    return hr;
  }

  return S_OK;
}

HRESULT HelpUrlBuilder::BuildHttpGetString(
    const CString& service_url,
    const std::vector<AppResult>& app_results,
    const CString& goopdate_version,
    const CString& source_id,
    CString* get_request) const {
  ASSERT1(get_request);
  if (service_url.IsEmpty()) {
    return E_INVALIDARG;
  }
  ASSERT1(_T('?') == service_url.GetAt(service_url.GetLength() - 1) ||
          _T('&') == service_url.GetAt(service_url.GetLength() - 1));

  CString os_version;
  CString service_pack;
  HRESULT hr = goopdate_utils::GetOSInfo(&os_version, &service_pack);
  if (FAILED(hr)) {
    CORE_LOG(LEVEL_WARNING, (_T("[GetOSInfo failed][0x%08x]"), hr));
  }
  const CString iid_string =
      ::IsEqualGUID(GUID_NULL, iid_) ? _T("") : GuidToString(iid_);

  std::vector<QueryElement> elements;
  elements.push_back(QueryElement(_T("hl"), language_));

  CString error_code_str;
  CString extra_code_str;
  CString element_name;
  for (std::vector<AppResult>::size_type i = 0; i < app_results.size(); ++i) {
    error_code_str.Format(_T("0x%x"), app_results[i].error_code);
    extra_code_str.Format(_T("%d"), app_results[i].extra_code);
    element_name.Format(_T("app.%d"), i);
    elements.push_back(QueryElement(element_name, app_results[i].guid));
    element_name.Format(_T("ec.%d"), i);
    elements.push_back(QueryElement(element_name, error_code_str));
    element_name.Format(_T("ex.%d"), i);
    elements.push_back(QueryElement(element_name, extra_code_str));
  }

  elements.push_back(QueryElement(_T("guver"), goopdate_version));
  elements.push_back(QueryElement(_T("m"), is_machine_ ? _T("1") : _T("0")));
  elements.push_back(QueryElement(_T("os"), os_version));
  elements.push_back(QueryElement(_T("sp"), service_pack));
  elements.push_back(QueryElement(_T("iid"), iid_string));
  elements.push_back(QueryElement(_T("brand"), brand_));
  elements.push_back(QueryElement(_T("source"), source_id));

  CString test_source = ConfigManager::Instance()->GetTestSource();
  if (!test_source.IsEmpty()) {
    elements.push_back(QueryElement(_T("testsource"), test_source));
  }

  CString query;
  hr = BuildQueryString(elements, &query);
  if (FAILED(hr)) {
    CORE_LOG(LEVEL_WARNING, (_T("[BuildQueryString failed][0x%08x]"), hr));
    return hr;
  }
  get_request->FormatMessage(_T("%1%2"), service_url, query);

  // The length should be smaller than the maximum allowed get length.
  ASSERT1(get_request->GetLength() <= INTERNET_MAX_URL_LENGTH);
  if (get_request->GetLength() > INTERNET_MAX_URL_LENGTH) {
    return E_FAIL;
  }

  return S_OK;
}

}  // namespace omaha
