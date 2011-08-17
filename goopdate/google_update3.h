// Copyright 2009-2010 Google Inc.
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

#ifndef OMAHA_GOOPDATE_GOOGLE_UPDATE3_H_
#define OMAHA_GOOPDATE_GOOGLE_UPDATE3_H_

#include <atlbase.h>
#include <atlcom.h>
#include <atlstr.h>
#include <vector>
#include "goopdate/omaha3_idl.h"
#include "base/scoped_ptr.h"
#include "omaha/base/atlregmapex.h"
#include "omaha/base/constants.h"
#include "omaha/base/error.h"
#include "omaha/base/exception_barrier.h"
#include "omaha/base/preprocessor_fun.h"
#include "omaha/base/user_rights.h"
#include "omaha/base/utils.h"
#include "omaha/common/goopdate_utils.h"
#include "omaha/goopdate/com_proxy.h"
#include "omaha/goopdate/model.h"
#include "omaha/goopdate/non_localized_resource.h"
#include "omaha/goopdate/worker.h"
#include "third_party/bar/shared_ptr.h"

namespace omaha {

// The ATL Singleton Class Factory does not work very well if errors happen in
// CreateInstance(), the server continues running. This is because the module
// count is not incremented or decremented. This class fixes the issue so that
// on error, the server shuts down as expected.
template <class T>
class SingletonClassFactory : public CComClassFactorySingleton<T> {
 public:
  SingletonClassFactory() {}
  virtual ~SingletonClassFactory() {}

  STDMETHOD(CreateInstance)(LPUNKNOWN unk, REFIID iid, void** obj) {
    HRESULT hr = CComClassFactorySingleton<T>::CreateInstance(unk, iid, obj);
    if (FAILED(hr)) {
      CORE_LOG(LE, (_T("[SingletonClassFactory::CreateInstance failed][0x%x]")
                    _T("[pulsing module count]"), hr));
      LockServer(TRUE);
      LockServer(FALSE);

      return hr;
    }

    return hr;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(SingletonClassFactory);
};

template <bool machine, const TCHAR* const progid, const GUID& clsid,
          UINT registry_resid, const TCHAR* const hkroot>
struct Update3COMClassMode {
  static bool is_machine() { return machine; }
  static const TCHAR* const prog_id() { return progid; }
  static GUID class_id() { return clsid; }
  static UINT registry_res_id() { return registry_resid; }
  static const TCHAR* const hk_root() { return hkroot; }
};

#pragma warning(push)
// C4640: construction of local static object is not thread-safe
#pragma warning(disable : 4640)
// C4505: unreferenced IUnknown local functions have been removed
#pragma warning(disable : 4505)

template <typename T>
class ATL_NO_VTABLE Update3COMClass
    : public CComObjectRootEx<CComObjectThreadModel>,
      public CComCoClass<Update3COMClass<T> >,
      public IDispatchImpl<IGoogleUpdate3,
                           &__uuidof(IGoogleUpdate3),
                           &CAtlModule::m_libid,
                           kMajorTypeLibVersion,
                           kMinorTypeLibVersion>,
      public StdMarshalInfo {
 public:
  typedef Update3COMClass<T> Update3COMClassT;
  typedef SingletonClassFactory<Update3COMClassT> SingletonClassFactoryT;

  Update3COMClass() : StdMarshalInfo(T::is_machine()), model_(NULL) {}
  virtual ~Update3COMClass() {}

  DECLARE_CLASSFACTORY_EX(SingletonClassFactoryT)
  DECLARE_NOT_AGGREGATABLE(Update3COMClassT)
  DECLARE_PROTECT_FINAL_CONSTRUCT()

  DECLARE_REGISTRY_RESOURCEID_EX(T::registry_res_id())

  BEGIN_REGISTRY_MAP()
    REGMAP_ENTRY(_T("HKROOT"), T::hk_root())
    REGMAP_EXE_MODULE(_T("MODULE"))
    REGMAP_ENTRY(_T("VERSION"), _T("1.0"))
    REGMAP_ENTRY(_T("PROGID"), T::prog_id())
    REGMAP_ENTRY(_T("DESCRIPTION"), _T("Update3COMClass"))
    REGMAP_UUID(_T("CLSID"), T::class_id())
  END_REGISTRY_MAP()

  BEGIN_COM_MAP(Update3COMClassT)
    COM_INTERFACE_ENTRY(IGoogleUpdate3)
    COM_INTERFACE_ENTRY(IDispatch)
    COM_INTERFACE_ENTRY(IStdMarshalInfo)
  END_COM_MAP()

  STDMETHODIMP get_Count(long* count) {  // NOLINT
    ASSERT1(count);
    ExceptionBarrier barrier;

    __mutexScope(model()->lock());
    *count = model()->GetNumberOfAppBundles();

    return S_OK;
  }

  STDMETHODIMP get_Item(long index, IDispatch** app_bundle_wrapper) {  // NOLINT
    ASSERT1(app_bundle_wrapper);
    ExceptionBarrier barrier;

    if (::IsUserAnAdmin() && !UserRights::VerifyCallerIsAdmin()) {
      CORE_LOG(LE, (_T("[User is not an admin]")));
      return E_ACCESSDENIED;
    }

    __mutexScope(model()->lock());

    const size_t num_app_bundles(model()->GetNumberOfAppBundles());
    if (index < 0 || static_cast<size_t>(index) >= num_app_bundles) {
      return HRESULT_FROM_WIN32(ERROR_INVALID_INDEX);
    }
    shared_ptr<AppBundle> app_bundle(model()->GetAppBundle(index));
    return AppBundleWrapper::Create(app_bundle->controlling_ptr(),
                                    app_bundle.get(),
                                    app_bundle_wrapper);
  }

  // Creates an AppBundle object and its corresponding COM wrapper.
  STDMETHODIMP createAppBundle(IDispatch** app_bundle_wrapper) {
    ASSERT1(app_bundle_wrapper);
    ExceptionBarrier barrier;

    __mutexScope(model()->lock());

    shared_ptr<AppBundle> app_bundle(model()->CreateAppBundle(T::is_machine()));
    return AppBundleWrapper::Create(app_bundle->controlling_ptr(),
                                    app_bundle.get(),
                                    app_bundle_wrapper);
  }

  HRESULT FinalConstruct() {
    CORE_LOG(L2, (_T("[Update3COMClass::FinalConstruct]")));

    HRESULT hr = InitializeWorker();
    if (FAILED(hr)) {
      CORE_LOG(LE, (_T("[InitializeWorker failed][0x%x]"), hr));
      return hr;
    }

    omaha::interlocked_exchange_pointer(&model_, Worker::Instance().model());
    ASSERT1(model());

    return S_OK;
  }

  void FinalRelease() {
    CORE_LOG(L2, (_T("[Update3COMClass::FinalRelease]")));
  }

 private:
  static HRESULT InitializeWorker() {
    static LLock lock;
    static bool is_initialized = false;

    __mutexScope(lock);

    if (is_initialized) {
      return S_OK;
    }

    CORE_LOG(L2, (_T("[InitializeWorker][%d]"), T::is_machine()));

    HRESULT hr = Worker::Instance().Initialize(T::is_machine());
    if (FAILED(hr)) {
      return hr;
    }

    is_initialized = true;
    return S_OK;
  }

  Model* model() {
    return omaha::interlocked_exchange_pointer(&model_, model_);
  }

  // C++ root of the object model. Not owned by this instance.
  mutable Model* volatile model_;

  DISALLOW_COPY_AND_ASSIGN(Update3COMClass);
};

#pragma warning(pop)

extern TCHAR kHKRootUser[];
extern TCHAR kHKRootMachine[];
extern TCHAR kHKRootService[];
extern TCHAR kProgIDUpdate3COMClassUserLocal[];
extern TCHAR kProgIDUpdate3COMClassMachineLocal[];
extern TCHAR kProgIDUpdate3COMClassServiceLocal[];

typedef Update3COMClassMode<false,
                            kProgIDUpdate3COMClassUserLocal,
                            __uuidof(GoogleUpdate3UserClass),
                            IDR_LOCAL_SERVER_RGS,
                            kHKRootUser> Update3COMClassModeUser;

typedef Update3COMClassMode<true,
                            kProgIDUpdate3COMClassServiceLocal,
                            __uuidof(GoogleUpdate3ServiceClass),
                            IDR_LOCAL_SERVICE_RGS,
                            kHKRootService> Update3COMClassModeService;

typedef Update3COMClass<Update3COMClassModeUser> Update3COMClassUser;
typedef Update3COMClass<Update3COMClassModeService> Update3COMClassService;

}  // namespace omaha

#endif  // OMAHA_GOOPDATE_GOOGLE_UPDATE3_H_
