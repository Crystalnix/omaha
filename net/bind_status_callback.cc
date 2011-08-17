// Copyright 2007-2009 Google Inc.
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
//
// IBindStatusCallback interface.

#include "omaha/net/bind_status_callback.h"
#include <wininet.h>
#include "omaha/base/debug.h"
#include "omaha/base/error.h"
#include "omaha/base/logging.h"
#include "omaha/base/utils.h"

namespace omaha {

HRESULT QueryHttpInfo(IWinInetHttpInfo* http_info, DWORD query, CString* info) {
  CORE_LOG(L3, (_T("[QueryHttpInfo][%d]"), query));
  ASSERT1(http_info);
  ASSERT1(query);
  ASSERT1(info);

  info->Empty();
  DWORD size = 0;
  DWORD flags = 0;
  HRESULT hr = http_info->QueryInfo(query, 0, &size, &flags, 0);
  CORE_LOG(L3, (_T("[http_info->QueryInfo][0x%x][%d]"), hr, size));
  if (FAILED(hr)) {
    return hr;
  }

  CStringA buf;
  hr = http_info->QueryInfo(query, CStrBufA(buf, size), &size, &flags, 0);
  CORE_LOG(L3, (_T("[http_info->QueryInfo][0x%x][%d]"), hr, size));
  if (FAILED(hr)) {
    return hr;
  }

  CORE_LOG(L3, (_T("[QueryHttpInfo success][%d][%s]"), query, CA2T(buf)));
  *info = buf;
  return S_OK;
}

BindStatusCallback::BindStatusCallback()
    : http_verb_(BINDVERB_GET),
      post_data_byte_count_(0),
      response_code_(0) {
}

HRESULT BindStatusCallback::Send(BSTR url,
                                 BSTR post_data,
                                 BSTR request_headers,
                                 VARIANT response_headers_needed,
                                 VARIANT* response_headers,
                                 DWORD* response_code,
                                 BSTR* cache_filename) {
  if (!url || !*url || !response_code || !cache_filename) {
    return E_INVALIDARG;
  }

  *response_code = 0;
  *cache_filename = NULL;

  if (V_VT(&response_headers_needed) != VT_EMPTY) {
    if ((V_VT(&response_headers_needed) != (VT_ARRAY | VT_UI4)) ||
        !response_headers) {
      return E_INVALIDARG;
    }
    V_VT(response_headers) = VT_NULL;
    response_headers_needed_ = response_headers_needed.parray;
    if (!response_headers_needed_.GetCount()) {
      return E_INVALIDARG;
    }
  }

  request_headers_ = request_headers;
  if (!post_data) {
    http_verb_ = BINDVERB_GET;
  } else {
    http_verb_ = BINDVERB_POST;
    post_data_byte_count_ = ::SysStringByteLen(post_data);
    reset(post_data_, ::GlobalAlloc(GPTR, post_data_byte_count_));
    if (!post_data_) {
      HRESULT hr = HRESULTFromLastError();
      CORE_LOG(LE, (_T("[::GlobalAlloc failed][0x%x]"), hr));
      return hr;
    }

    memcpy(get(post_data_), post_data, post_data_byte_count_);
  }

  CComPtr<IBindStatusCallback> bsc(this);
  CString filename;
  HRESULT hr = ::URLDownloadToCacheFile(NULL,
                                        url,
                                        CStrBuf(filename, MAX_PATH),
                                        MAX_PATH,
                                        0,
                                        bsc);

  if (response_headers) {
    response_headers_.Detach(response_headers);
  }
  *response_code = response_code_;

  CORE_LOG(L2, (_T("[URLDownloadToCacheFile][0x%x][%s]"), hr, url));
  if (FAILED(hr)) {
    return hr;
  }

  ASSERT1(!filename.IsEmpty());
  CORE_LOG(L2, (_T("[BindStatusCallback::Send][cache file][%s]"), filename));
  *cache_filename = filename.AllocSysString();
  return hr;
}

// IBindStatusCallback methods.

STDMETHODIMP BindStatusCallback::OnStartBinding(DWORD, IBinding* binding) {
  __mutexScope(lock_);
  binding_git_.Attach(binding);
  return S_OK;
}

STDMETHODIMP BindStatusCallback::GetPriority(LONG* priority) {
  UNREFERENCED_PARAMETER(priority);
  return E_NOTIMPL;
}

STDMETHODIMP BindStatusCallback::OnLowResource(DWORD) {
  return E_NOTIMPL;
}

STDMETHODIMP BindStatusCallback::OnProgress(ULONG, ULONG, ULONG, LPCWSTR) {
  return E_NOTIMPL;
}

STDMETHODIMP BindStatusCallback::OnStopBinding(HRESULT, LPCWSTR) {
  CComPtr<IBinding> binding;

  __mutexBlock(lock_) {
    if (!binding_git_) {
      return S_OK;
    }

    HRESULT hr = binding_git_.CopyTo(&binding);
    VERIFY1(SUCCEEDED(binding_git_.Revoke()));
    if (FAILED(hr)) {
      CORE_LOG(LW, (_T("[binding_git_.CopyTo failed][0x%x]"), hr));
      return S_OK;
    }
  }

  CComQIPtr<IWinInetHttpInfo> http_info(binding);
  if (!http_info) {
    return S_OK;
  }

  CString response_code_buf;
  if (SUCCEEDED(QueryHttpInfo(http_info,
                              HTTP_QUERY_STATUS_CODE,
                              &response_code_buf)) &&
      !response_code_buf.IsEmpty()) {
    response_code_ = _ttoi(response_code_buf);
  }

  if (!response_headers_needed_) {
    return S_OK;
  }
  int count = response_headers_needed_.GetCount();
  ASSERT1(count > 0);
  int lower_bound = response_headers_needed_.GetLowerBound();
  int upper_bound = response_headers_needed_.GetUpperBound();

  CComSafeArray<BSTR> response_array(count, lower_bound);
  for (int i = lower_bound; i <= upper_bound; ++i) {
    CString response_header_buf;
    QueryHttpInfo(http_info, response_headers_needed_[i], &response_header_buf);
    response_array[i] = response_header_buf.AllocSysString();
  }

  V_VT(&response_headers_) = VT_ARRAY | VT_BSTR;
  V_ARRAY(&response_headers_) = response_array.Detach();
  return S_OK;
}

STDMETHODIMP BindStatusCallback::GetBindInfo(DWORD* flags, BINDINFO* info) {
  ASSERT1(flags);
  ASSERT1(info);
  *flags = 0;

  // Set up the BINDINFO data structure.
  info->cbSize = sizeof(*info);
  info->dwBindVerb = http_verb_;
  info->szExtraInfo = NULL;

  // Initialize the STGMEDIUM.
  SetZero(info->stgmedData);
  info->grfBindInfoF = 0;
  info->szCustomVerb = NULL;

  switch (http_verb_) {
    case BINDVERB_POST:
      if (post_data_) {
        // Fill the STGMEDIUM with the data to post. Certain versions of Urlmon
        // require TYMED_GLOBAL with GMEM_FIXED.
        info->stgmedData.tymed = TYMED_HGLOBAL;
        info->stgmedData.hGlobal = get(post_data_);

        // The documentation for GetBindInfo() indicates that the method could
        // be called multiple times for the same request. We do not want to
        // allocate global memory for each of those times. So we maintain
        // ownership of the global memory, and pass a reference to it each time.
        // The HGLOBAL is released on BindStatusCallback destruction. Hence we
        // set pUnkForRelease to our IUnknown ptr.
        info->stgmedData.pUnkForRelease =
            static_cast<IBindStatusCallback*>(this);
        AddRef();

        info->cbstgmedData = post_data_byte_count_;
      }
      return S_OK;

    case BINDVERB_GET:
      return S_OK;

    case BINDVERB_PUT:
    case BINDVERB_CUSTOM:
    default:
      ASSERT1(false);
      return E_FAIL;
  }
}

STDMETHODIMP BindStatusCallback::OnDataAvailable(DWORD,
                                                 DWORD,
                                                 FORMATETC*,
                                                 STGMEDIUM*) {
  // The documentation does not explicitly say that E_NOTIMPL can be returned
  // for this method. So we return S_OK.
  return S_OK;
}

STDMETHODIMP BindStatusCallback::OnObjectAvailable(REFIID, IUnknown*) {
  // The documentation does not explicitly say that E_NOTIMPL can be returned
  // for this method. So we return S_OK.
  return S_OK;
}

STDMETHODIMP BindStatusCallback::BeginningTransaction(LPCWSTR,
                                                      LPCWSTR,
                                                      DWORD,
                                                      LPWSTR* request_headers) {
  if (!request_headers) {
    return E_INVALIDARG;
  }
  *request_headers = NULL;

  if (request_headers_.IsEmpty()) {
    return S_OK;
  }

  int request_headers_size = request_headers_.GetLength() + 1;
  TCHAR* additional_headers = static_cast<TCHAR*>(
      ::CoTaskMemAlloc(request_headers_size * sizeof(TCHAR)));
  if (!additional_headers) {
    return E_OUTOFMEMORY;
  }

  _tcscpy_s(additional_headers, request_headers_size, request_headers_);
  *request_headers = additional_headers;

  return S_OK;
}

STDMETHODIMP BindStatusCallback::OnResponse(DWORD response_code,
                                            LPCWSTR response_headers,
                                            LPCWSTR request_headers,
                                            LPWSTR* additional_headers) {
  CORE_LOG(L1, (_T("[OnResponse [%d][%s]"), response_code, response_headers));
  UNREFERENCED_PARAMETER(response_code);
  UNREFERENCED_PARAMETER(response_headers);
  UNREFERENCED_PARAMETER(request_headers);
  if (!additional_headers) {
    return E_INVALIDARG;
  }

  *additional_headers = NULL;
  return S_OK;
}

HRESULT BindStatusCallback::Cancel() {
  CComPtr<IBinding> binding;

  __mutexBlock(lock_) {
    if (!binding_git_) {
      return S_OK;
    }

    HRESULT hr = binding_git_.CopyTo(&binding);
    if (FAILED(hr)) {
      CORE_LOG(LE, (_T("[binding_git_.CopyTo failed][0x%x]"), hr));
      return hr;
    }
  }

  return binding->Abort();
}

}  // namespace omaha

