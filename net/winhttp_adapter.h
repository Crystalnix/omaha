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

#ifndef OMAHA_NET_WINHTTP_ADAPTER_H_
#define OMAHA_NET_WINHTTP_ADAPTER_H_

#include <windows.h>
#include "base/basictypes.h"
#include "base/scoped_ptr.h"
#include "omaha/base/scoped_any.h"
#include "omaha/base/synchronized.h"
#include "omaha/net/winhttp.h"

namespace omaha {

// Provides a sync-async adapter between the caller and the asynchronous
// WinHttp client. Solves the issue of reliably canceling of WinHttp calls by
// closing the handles and avoding the race condition between handle closing
// and the incoming WinHttp call.
// The class manages the connection and the request handles. It registers a
// callback for all WinHttp status notifications. Once an asynchrous WinHttp
// call is made, the code blocks waiting for the corresponding notification
// to arrive, handle the completion result, and then return to the caller.
// WinHttp is guaranteed to send a notification callback for all asynchronous
// request calls that have succeeded.
// TODO(omaha): consider eliminating this class and implementing the same
// functionality in the WinHttp class. Most likely, another class is needed
// to manage the WinHttp session handle.
class WinHttpAdapter {
 public:
  WinHttpAdapter();

  HRESULT Initialize();

  HRESULT Connect(HINTERNET session_handle, const TCHAR* server, int port);

  HRESULT OpenRequest(const TCHAR* verb,
                      const TCHAR* uri,
                      const TCHAR* version,
                      const TCHAR* referrer,
                      const TCHAR** accept_types,
                      uint32 flags);

  HRESULT AddRequestHeaders(const TCHAR* headers,
                            int length,
                            uint32 modifiers);

  HRESULT SendRequest(const TCHAR* headers,
                      DWORD headers_length,
                      const void* optional_data,
                      DWORD optional_data_length,
                      DWORD content_length);

  HRESULT SetCredentials(uint32 auth_targets,
                         uint32 auth_scheme,
                         const TCHAR* user_name,
                         const TCHAR* password);

  HRESULT ReceiveResponse();

  HRESULT QueryAuthSchemes(uint32* supported_schemes,
                           uint32* first_scheme,
                           uint32* auth_target);

  HRESULT QueryRequestHeadersInt(uint32 info_level,
                                 const TCHAR* name,
                                 int* value,
                                 DWORD* index);

  HRESULT QueryRequestHeadersString(uint32 info_level,
                                    const TCHAR* name,
                                    CString* value,
                                    DWORD* index);

  HRESULT QueryDataAvailable(DWORD* num_bytes);

  HRESULT ReadData(void* buffer, DWORD buffer_length, DWORD* bytes_read);

  HRESULT SetRequestOptionInt(uint32 option, int value);

  HRESULT SetRequestOption(uint32 option,
                           const void* buffer,
                           DWORD buffer_length);

  void CloseHandles();

  HRESULT CrackUrl(const TCHAR* url,
                   uint32 flags,
                   CString* scheme,
                   CString* server,
                   int* port,
                   CString* url_path,
                   CString* extra_info) {
    return http_client_->CrackUrl(
        url, flags, scheme, server, port, url_path, extra_info);
  }

  CString server_name() const { return server_name_; }
  CString server_ip() const { return server_ip_; }

 private:

  HRESULT AsyncCallBegin(DWORD async_call_type);
  HRESULT AsyncCallEnd(DWORD async_call_type);

  void StatusCallback(HINTERNET handle,
                      uint32 status,
                      void* info,
                      uint32 info_len);

  static void __stdcall WinHttpStatusCallback(HINTERNET handle,
                                              uint32 context,
                                              uint32 status,
                                              void* info,
                                              uint32 info_len);

  scoped_ptr<HttpClient> http_client_;

  HINTERNET              connection_handle_;
  HINTERNET              request_handle_;

  CString                server_name_;
  CString                server_ip_;

  DWORD                  async_call_type_;
  bool                   async_call_is_error_;
  WINHTTP_ASYNC_RESULT   async_call_result_;
  DWORD                  async_bytes_available_;
  DWORD                  async_bytes_read_;
  scoped_event           async_completion_event_;

  LLock                  lock_;

  DISALLOW_COPY_AND_ASSIGN(WinHttpAdapter);
};

}  // namespace omaha

#endif  // OMAHA_NET_WINHTTP_ADAPTER_H_

