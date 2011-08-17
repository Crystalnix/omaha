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

#include "omaha/goopdate/cocreate_async.h"
#include "base/basictypes.h"
#include "base/debug.h"
#include "omaha/base/scope_guard.h"
#include "base/scoped_ptr_address.h"
#include "omaha/base/system.h"
#include "omaha/base/thread_pool_callback.h"
#include "omaha/base/vistautil.h"
#include "omaha/goopdate/goopdate.h"

namespace omaha {

CoCreateAsync::CoCreateAsync() : StdMarshalInfo(true) {
}

STDMETHODIMP CoCreateAsync::createOmahaMachineServerAsync(
    BSTR origin_url,
    BOOL create_elevated,
    ICoCreateAsyncStatus** status) {
  CORE_LOG(L3, (L"[CoCreateAsync::createOmahaMachineServerAsync][%s][%d]",
                origin_url, create_elevated));
  ASSERT1(status);
  ASSERT1(origin_url && wcslen(origin_url));
  *status = NULL;

  if (create_elevated &&
      !vista_util::IsVistaOrLater() && !vista_util::IsUserAdmin()) {
    return E_ACCESSDENIED;
  }

  typedef CComObject<CoCreateAsyncStatus> ComObjectAsyncStatus;
  scoped_ptr<ComObjectAsyncStatus> async_status;
  HRESULT hr = ComObjectAsyncStatus::CreateInstance(address(async_status));
  if (FAILED(hr)) {
    return hr;
  }

  hr = async_status->CreateOmahaMachineServerAsync(origin_url, create_elevated);
  if (FAILED(hr)) {
    return hr;
  }

  hr = async_status->QueryInterface(status);
  if (FAILED(hr)) {
    return hr;
  }

  async_status.release();
  return S_OK;
}

CoCreateAsyncStatus::CoCreateAsyncStatus() : is_done_(false), hr_(E_PENDING) {
}

HRESULT CoCreateAsyncStatus::CreateOmahaMachineServerAsync(
    BSTR origin_url,
    BOOL create_elevated) {
  // Create a thread pool work item for deferred execution of the CoCreate. The
  // thread pool owns this call back object.
  typedef ThreadPoolCallBack2<CoCreateAsyncStatus,
                              const CString,
                              BOOL> CallBack;
  scoped_ptr<CallBack>
      callback(new CallBack(this,
                            &CoCreateAsyncStatus::CreateOmahaMachineServer,
                            origin_url,
                            create_elevated));
  HRESULT hr = Goopdate::Instance().QueueUserWorkItem(callback.get(),
                                                      WT_EXECUTELONGFUNCTION);
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[QueueUserWorkItem failed][0x%x]"), hr));
    return hr;
  }

  VERIFY1(thread_started_gate_.Wait(INFINITE));

  callback.release();
  return S_OK;
}

void CoCreateAsyncStatus::CreateOmahaMachineServer(const CString origin_url,
                                                   BOOL create_elevated) {
  CORE_LOG(L3, (_T("[CoCreateAsyncStatus::CreateOmahaMachineServer][%s][%d]"),
                origin_url, create_elevated));
  AddRef();
  ON_SCOPE_EXIT_OBJ(*this, &CoCreateAsyncStatus::Release);

  VERIFY1(thread_started_gate_.Open());

  HRESULT hr = E_FAIL;
  CComPtr<IDispatch> ptr;

  // Since the values of hr and ptr are being modified after the scope guard,
  // the variables are passed by reference instead of by values using ByRef.
  ON_SCOPE_EXIT_OBJ(*this,
                    &CoCreateAsyncStatus::SetCreateInstanceResults,
                    ByRef(hr),
                    ByRef(ptr));

  scoped_co_init init_com_apt(COINIT_MULTITHREADED);
  hr = init_com_apt.hresult();
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[init_com_apt failed][0x%x]"), hr));
    return;
  }

  CComPtr<IGoogleUpdate3WebSecurity> security;
  REFCLSID clsid(__uuidof(GoogleUpdate3WebMachineClass));
  hr = create_elevated ?
      System::CoCreateInstanceAsAdmin(NULL, clsid, IID_PPV_ARGS(&security)) :
      ::CoCreateInstance(clsid, NULL, CLSCTX_ALL, IID_PPV_ARGS(&security));
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[CoCreate failed][0x%x]"), hr));
    return;
  }

  hr = security->setOriginURL(CComBSTR(origin_url));
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[setOriginURL failed][0x%x]"), hr));
    return;
  }

  hr = security.QueryInterface(&ptr);
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[QueryInterface failed][0x%x]"), hr));
    return;
  }
}

void CoCreateAsyncStatus::SetCreateInstanceResults(
    const HRESULT& hr,
    const CComPtr<IDispatch>& ptr) {
  CORE_LOG(L3, (_T("[SetCreateInstanceResults][0x%x][0x%p]"), hr, ptr));
  Lock();
  ON_SCOPE_EXIT_OBJ(*this, &CoCreateAsyncStatus::Unlock);

  hr_ = hr;
  ptr_ = ptr;
  is_done_ = true;
}

// ICoCreateAsyncStatus.
STDMETHODIMP CoCreateAsyncStatus::get_isDone(VARIANT_BOOL* is_done) {
  Lock();
  ON_SCOPE_EXIT_OBJ(*this, &CoCreateAsyncStatus::Unlock);

  ASSERT1(is_done);

  *is_done = is_done_ ? VARIANT_TRUE : VARIANT_FALSE;
  CORE_LOG(L3, (_T("[get_isDone][%d]"), is_done_));
  return S_OK;
}

STDMETHODIMP CoCreateAsyncStatus::get_completionHResult(LONG* hr) {
  Lock();
  ON_SCOPE_EXIT_OBJ(*this, &CoCreateAsyncStatus::Unlock);

  ASSERT1(hr);

  *hr = hr_;
  CORE_LOG(L3, (_T("[get_completionHResult][0x%x]"), hr_));
  return S_OK;
}

STDMETHODIMP CoCreateAsyncStatus::get_createdInstance(IDispatch** instance) {
  Lock();
  ON_SCOPE_EXIT_OBJ(*this, &CoCreateAsyncStatus::Unlock);

  ASSERT1(instance);

  ptr_.CopyTo(instance);
  CORE_LOG(L3, (_T("[get_createdInstance][0x%p]"), *instance));
  return S_OK;
}

}  // namespace omaha

