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

#include "omaha/net/winhttp_adapter.h"
#include <memory>
#include "omaha/base/debug.h"
#include "omaha/base/error.h"
#include "omaha/base/logging.h"

namespace omaha {

WinHttpAdapter::WinHttpAdapter()
    : connection_handle_(NULL),
      request_handle_(NULL),
      async_call_type_(0),
      async_call_is_error_(0),
      async_bytes_available_(0),
      async_bytes_read_(0) {
  memset(&async_call_result_, 0, sizeof(async_call_result_));
}

HRESULT WinHttpAdapter::Initialize() {
  __mutexScope(lock_);

  http_client_.reset(CreateHttpClient());

  HRESULT hr = http_client_->Initialize();
  if (FAILED(hr)) {
    return hr;
  }

  reset(async_completion_event_, ::CreateEvent(NULL, true, false, NULL));

  return async_completion_event_ ? S_OK : HRESULTFromLastError();
}

void WinHttpAdapter::CloseHandles() {
  __mutexScope(lock_);

  if (request_handle_) {
    VERIFY1(SUCCEEDED(http_client_->Close(request_handle_)));
    request_handle_ = NULL;
  }
  if (connection_handle_) {
    VERIFY1(SUCCEEDED(http_client_->Close(connection_handle_)));
    connection_handle_ = NULL;
  }
}

HRESULT WinHttpAdapter::Connect(HINTERNET session_handle,
                                const TCHAR* server,
                                int port) {
  __mutexScope(lock_);

  HRESULT hr = http_client_->Connect(session_handle,
                                     server,
                                     port,
                                     &connection_handle_);
  if (FAILED(hr)) {
    return hr;
  }

  HttpClient::StatusCallback old_callback =
      http_client_->SetStatusCallback(connection_handle_,
                                      &WinHttpAdapter::WinHttpStatusCallback,
                                      WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS);
  ASSERT1(old_callback == NULL || old_callback == kInvalidStatusCallback);

  return http_client_->SetOptionInt(connection_handle_,
                                    WINHTTP_OPTION_CONTEXT_VALUE,
                                    reinterpret_cast<int>(this));
}

HRESULT WinHttpAdapter::OpenRequest(const TCHAR* verb,
                                    const TCHAR* uri,
                                    const TCHAR* version,
                                    const TCHAR* referrer,
                                    const TCHAR** accept_types,
                                    uint32 flags) {
  __mutexScope(lock_);

  return http_client_->OpenRequest(connection_handle_,
                                   verb,
                                   uri,
                                   version,
                                   referrer,
                                   accept_types,
                                   flags,
                                   &request_handle_);
}

HRESULT WinHttpAdapter::AddRequestHeaders(const TCHAR* headers,
                                          int length,
                                          uint32 modifiers) {
  __mutexScope(lock_);

  return http_client_->AddRequestHeaders(request_handle_,
                                         headers,
                                         length,
                                         modifiers);
}

HRESULT WinHttpAdapter::QueryAuthSchemes(uint32* supported_schemes,
                                         uint32* first_scheme,
                                         uint32* auth_target) {
  __mutexScope(lock_);

  return http_client_->QueryAuthSchemes(request_handle_,
                                        supported_schemes,
                                        first_scheme,
                                        auth_target);
}

HRESULT WinHttpAdapter::QueryRequestHeadersInt(uint32 info_level,
                                               const TCHAR* name,
                                               int* value,
                                               DWORD* index) {
  __mutexScope(lock_);

  return http_client_->QueryHeadersInt(request_handle_,
                                       info_level,
                                       name,
                                       value,
                                       index);
}

HRESULT WinHttpAdapter::QueryRequestHeadersString(uint32 info_level,
                                                  const TCHAR* name,
                                                  CString* value,
                                                  DWORD* index) {
  __mutexScope(lock_);

  return http_client_->QueryHeadersString(request_handle_,
                                          info_level,
                                          name,
                                          value,
                                          index);
}

HRESULT WinHttpAdapter::SetCredentials(uint32 auth_targets,
                                       uint32 auth_scheme,
                                       const TCHAR* user_name,
                                       const TCHAR* password) {
  __mutexScope(lock_);

  return http_client_->SetCredentials(request_handle_,
                                      auth_targets,
                                      auth_scheme,
                                      user_name,
                                      password);
}

HRESULT WinHttpAdapter::SendRequest(const TCHAR* headers,
                                    DWORD headers_length,
                                    const void* optional_data,
                                    DWORD optional_data_length,
                                    DWORD content_length) {
  __mutexScope(lock_);

  HRESULT hr = AsyncCallBegin(API_SEND_REQUEST);
  if (FAILED(hr)) {
    return hr;
  }

  const DWORD_PTR context = reinterpret_cast<DWORD_PTR>(this);
  hr = http_client_->SendRequest(request_handle_,
                                 headers,
                                 headers_length,
                                 optional_data,
                                 optional_data_length,
                                 content_length,
                                 context);
  if (FAILED(hr)) {
    return hr;
  }

  return AsyncCallEnd(API_SEND_REQUEST);
}

HRESULT WinHttpAdapter::ReceiveResponse() {
  __mutexScope(lock_);

  HRESULT hr = AsyncCallBegin(API_RECEIVE_RESPONSE);
  if (FAILED(hr)) {
    return hr;
  }

  hr = http_client_->ReceiveResponse(request_handle_);
  if (FAILED(hr)) {
    return hr;
  }

  return AsyncCallEnd(API_RECEIVE_RESPONSE);
}

HRESULT WinHttpAdapter::QueryDataAvailable(DWORD* num_bytes) {
  __mutexScope(lock_);

  HRESULT hr = AsyncCallBegin(API_QUERY_DATA_AVAILABLE);
  if (FAILED(hr)) {
    return hr;
  }

  async_bytes_available_ = 0;

  hr = http_client_->QueryDataAvailable(request_handle_, NULL);
  if (FAILED(hr)) {
    return hr;
  }

  hr =  AsyncCallEnd(API_QUERY_DATA_AVAILABLE);
  if (FAILED(hr)) {
    return hr;
  }

  *num_bytes = async_bytes_available_;

  return S_OK;
}

HRESULT WinHttpAdapter::ReadData(void* buffer,
                                 DWORD buffer_length,
                                 DWORD* bytes_read) {
  __mutexScope(lock_);

  HRESULT hr = AsyncCallBegin(API_READ_DATA);
  if (FAILED(hr)) {
    return hr;
  }

  async_bytes_read_ = 0;

  hr = http_client_->ReadData(request_handle_,
                              buffer,
                              buffer_length,
                              NULL);
  if (FAILED(hr)) {
    return hr;
  }

  hr = AsyncCallEnd(API_READ_DATA);
  if (FAILED(hr)) {
    return hr;
  }

  *bytes_read = async_bytes_read_;

  return S_OK;
}

HRESULT WinHttpAdapter::SetRequestOptionInt(uint32 option, int value) {
  __mutexScope(lock_);

  return http_client_->SetOptionInt(request_handle_, option, value);
}

HRESULT WinHttpAdapter::SetRequestOption(uint32 option,
                                         const void* buffer,
                                         DWORD buffer_length) {
  __mutexScope(lock_);

  ASSERT1(buffer && buffer_length);

  return http_client_->SetOption(request_handle_,
                                 option,
                                 buffer,
                                 buffer_length);
}

HRESULT WinHttpAdapter::AsyncCallBegin(DWORD async_call_type) {
  async_call_type_  = async_call_type;
  async_call_is_error_ = false;

  memset(&async_call_result_, 0, sizeof(async_call_result_));

  return ::ResetEvent(get(async_completion_event_)) ? S_OK :
                                                      HRESULTFromLastError();
}

// Waits for the WinHttp notification to arrive and handles the result of
// the asynchronous call.
HRESULT WinHttpAdapter::AsyncCallEnd(DWORD async_call_type) {
  UNREFERENCED_PARAMETER(async_call_type);

  const DWORD result = ::WaitForSingleObject(get(async_completion_event_),
                                             INFINITE);
  ASSERT1(result == WAIT_OBJECT_0);
  switch (result) {
    case WAIT_OBJECT_0:
      break;
    case WAIT_TIMEOUT:
      return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    case WAIT_FAILED:
      return HRESULTFromLastError();
  }

  if (async_call_is_error_) {
    ASSERT1(async_call_result_.dwResult == async_call_type);
    ASSERT1(async_call_result_.dwError != ERROR_SUCCESS);
    return HRESULT_FROM_WIN32(async_call_result_.dwError);
  }

  return S_OK;
}

void WinHttpAdapter::StatusCallback(HINTERNET handle,
                                    uint32 status,
                                    void* info,
                                    uint32 info_len) {
  UNREFERENCED_PARAMETER(handle);

  switch (status) {
    case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
      ASSERT1(async_call_type_ == API_QUERY_DATA_AVAILABLE);

      ASSERT1(info_len == sizeof(async_bytes_available_));
      ASSERT1(info);
      async_bytes_available_ = *reinterpret_cast<DWORD*>(info);
      break;

    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
      ASSERT1(async_call_type_ == API_RECEIVE_RESPONSE);
      break;

    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
      ASSERT1(async_call_type_ == API_READ_DATA);

      ASSERT1(info);
      async_bytes_read_ = info_len;
      break;

    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
      ASSERT1(async_call_type_ == API_SEND_REQUEST);
      break;

    case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE:
      ASSERT1(async_call_type_ == API_WRITE_DATA);
      break;

    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
      ASSERT1(async_call_type_ == API_QUERY_DATA_AVAILABLE  ||
              async_call_type_ == API_RECEIVE_RESPONSE      ||
              async_call_type_ == API_READ_DATA             ||
              async_call_type_ == API_SEND_REQUEST          ||
              async_call_type_ == API_WRITE_DATA);

      ASSERT1(info_len == sizeof(async_call_result_));
      ASSERT1(info);
      async_call_result_ = *reinterpret_cast<WINHTTP_ASYNC_RESULT*>(info);
      async_call_is_error_ = true;
      break;

    default:
      break;
  }

  if (status == WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE        ||
      status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE     ||
      status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE         ||
      status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE  ||
      status == WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE        ||
      status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
    ASSERT1(!IsHandleSignaled(get(async_completion_event_)));
    VERIFY1(::SetEvent(get(async_completion_event_)));
  }
}

void __stdcall WinHttpAdapter::WinHttpStatusCallback(HINTERNET handle,
                                                     uint32 context,
                                                     uint32 status,
                                                     void* info,
                                                     uint32 info_len) {
  ASSERT1(handle);
  ASSERT1(context);
  WinHttpAdapter* http_adapter = reinterpret_cast<WinHttpAdapter*>(context);

  CString status_string;
  CString info_string;
  switch (status) {
    case WINHTTP_CALLBACK_STATUS_HANDLE_CREATED:
      status_string = _T("handle created");
      break;
      case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
      status_string = _T("handle closing");
      break;
    case WINHTTP_CALLBACK_STATUS_RESOLVING_NAME:
      status_string = _T("resolving");
      info_string.SetString(static_cast<TCHAR*>(info), info_len);  // host name
      http_adapter->server_name_ = info_string;
      break;
    case WINHTTP_CALLBACK_STATUS_NAME_RESOLVED:
      status_string = _T("resolved");
      info_string.SetString(static_cast<TCHAR*>(info), info_len);  // host ip
      http_adapter->server_ip_ = info_string;
      break;
    case WINHTTP_CALLBACK_STATUS_CONNECTING_TO_SERVER:
      status_string = _T("connecting");
      info_string.SetString(static_cast<TCHAR*>(info), info_len);  // host ip

      // Server name resolving may be skipped in some cases. So populate server
      // name and IP if not yet done.
      if (http_adapter->server_name_.IsEmpty()) {
        http_adapter->server_name_= info_string;
      }
      if (http_adapter->server_ip_.IsEmpty()) {
        http_adapter->server_ip_ = info_string;
      }
      break;
    case WINHTTP_CALLBACK_STATUS_CONNECTED_TO_SERVER:
      status_string = _T("connected");
      info_string.SetString(static_cast<TCHAR*>(info), info_len);  // host ip
      break;
    case WINHTTP_CALLBACK_STATUS_SENDING_REQUEST:
      status_string = _T("sending");
      break;
    case WINHTTP_CALLBACK_STATUS_REQUEST_SENT:
      status_string = _T("sent");
      break;
    case WINHTTP_CALLBACK_STATUS_RECEIVING_RESPONSE:
      status_string = _T("receiving");
      break;
    case WINHTTP_CALLBACK_STATUS_RESPONSE_RECEIVED:
      status_string = _T("received");
      break;
    case WINHTTP_CALLBACK_STATUS_CLOSING_CONNECTION:
      status_string = _T("connection closing");
      break;
    case WINHTTP_CALLBACK_STATUS_CONNECTION_CLOSED:
      status_string = _T("connection closed");
      break;
    case WINHTTP_CALLBACK_STATUS_REDIRECT:
      status_string = _T("redirect");
      info_string.SetString(static_cast<TCHAR*>(info), info_len);  // url
      break;
    case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
      status_string = _T("data available");
      break;
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
      status_string = _T("headers available");
      break;
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
      status_string = _T("read complete");
      break;
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
      status_string = _T("send request complete");
      break;
    case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE:
      status_string = _T("write complete");
      break;
    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
      status_string = _T("request error");
      break;
    case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE:
      status_string = _T("https failure");
      ASSERT1(info);
      ASSERT1(info_len == sizeof(DWORD));
      info_string.Format(_T("0x%x"), *static_cast<DWORD*>(info));
      break;
    default:
      break;
  }

  CString log_line;
  log_line.AppendFormat(_T("[WinHttp status callback][handle=0x%08x]"), handle);
  if (!status_string.IsEmpty()) {
    log_line.AppendFormat(_T("[%s]"), status_string);
  } else {
    log_line.AppendFormat(_T("[0x%08x]"), status);
  }
  if (!info_string.IsEmpty()) {
    log_line.AppendFormat(_T("[%s]"), info_string);
  }
  NET_LOG(L3, (_T("%s"), log_line));

  http_adapter->StatusCallback(handle, status, info, info_len);
}


}  // namespace omaha

