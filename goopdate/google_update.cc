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
//
// Contains the ATL exe server registration.

#include "omaha/goopdate/google_update.h"
#include "goopdate/omaha3_idl.h"
#include "omaha/base/debug.h"
#include "omaha/base/error.h"
#include "omaha/base/logging.h"
#include "omaha/common/goopdate_utils.h"
#include "omaha/core/google_update_core.h"
#include "omaha/goopdate/broker_class_factory.h"
#include "omaha/goopdate/cocreate_async.h"
#include "omaha/goopdate/cred_dialog.h"
#include "omaha/goopdate/google_update3.h"
#include "omaha/goopdate/omaha3_idl_datax.h"
#include "omaha/goopdate/ondemand.h"
#include "omaha/goopdate/oneclick_process_launcher.h"
#include "omaha/goopdate/process_launcher.h"
#include "omaha/goopdate/update3web.h"
#include "omaha/goopdate/worker.h"

namespace omaha {

// Template arguments need to be non-const TCHAR arrays.
TCHAR kOnDemandMachineBrokerProgId[] = kProgIDOnDemandMachine;
TCHAR kUpdate3WebMachineBrokerProgId[] = kProgIDUpdate3WebMachine;
TCHAR kHKRootUser[] = _T("HKCU");
TCHAR kHKRootMachine[] = _T("HKLM");
TCHAR kProgIDUpdate3COMClassUserLocal[] = kProgIDUpdate3COMClassUser;

BEGIN_OBJECT_MAP(object_map_update3_user_mode)
  OBJECT_ENTRY(__uuidof(GoogleUpdate3UserClass), Update3COMClassUser)
END_OBJECT_MAP()

BEGIN_OBJECT_MAP(object_map_broker_machine_mode)
  OBJECT_ENTRY(__uuidof(OnDemandMachineAppsClass), OnDemandMachineBroker)
  OBJECT_ENTRY(__uuidof(GoogleUpdate3WebMachineClass), Update3WebMachineBroker)
  OBJECT_ENTRY(__uuidof(CoCreateAsyncClass), CoCreateAsync)
  OBJECT_ENTRY(__uuidof(OneClickMachineProcessLauncherClass),
                        OneClickProcessLauncher)
END_OBJECT_MAP()

BEGIN_OBJECT_MAP(object_map_ondemand_user_mode)
  OBJECT_ENTRY(__uuidof(GoogleUpdate3WebUserClass), Update3WebUser)
  OBJECT_ENTRY(__uuidof(OnDemandUserAppsClass), OnDemandUser)
  OBJECT_ENTRY(__uuidof(CredentialDialogUserClass), CredentialDialogUser)
  OBJECT_ENTRY(__uuidof(OneClickUserProcessLauncherClass),
                        OneClickProcessLauncher)
END_OBJECT_MAP()

BEGIN_OBJECT_MAP(object_map_ondemand_machine_mode)
  OBJECT_ENTRY(__uuidof(ProcessLauncherClass), ProcessLauncher)
  OBJECT_ENTRY(__uuidof(GoogleUpdateCoreMachineClass), GoogleUpdateCoreMachine)
  OBJECT_ENTRY(__uuidof(OnDemandMachineAppsFallbackClass),
               OnDemandMachineFallback)
  OBJECT_ENTRY(__uuidof(GoogleUpdate3WebMachineFallbackClass),
               Update3WebMachineFallback)
  OBJECT_ENTRY(__uuidof(CredentialDialogMachineClass), CredentialDialogMachine)
END_OBJECT_MAP()

_ATL_OBJMAP_ENTRY* GoogleUpdate::GetObjectMap() {
  if (mode_ == kUpdate3Mode && !is_machine_) {
    return object_map_update3_user_mode;
  }

  if (mode_ == kBrokerMode && is_machine_) {
    return object_map_broker_machine_mode;
  }

  if (mode_ == kOnDemandMode && !is_machine_) {
    return object_map_ondemand_user_mode;
  }

  if (mode_ == kOnDemandMode && is_machine_) {
    return object_map_ondemand_machine_mode;
  }

  return NULL;
}

GoogleUpdate::GoogleUpdate(bool is_machine, ComServerMode mode)
    : is_machine_(is_machine), mode_(mode) {
  // Disable the delay on shutdown mechanism in CAtlExeModuleT.
  m_bDelayShutdown = false;
}

GoogleUpdate::~GoogleUpdate() {
  // GoogleUpdate is typically created on the stack. We reset the _pAtlModule
  // here, to allow for cases such as /RegServer, where multiple instances of
  // GoogleUpdate are created and destroyed serially.
  _pAtlModule = NULL;
}

HRESULT GoogleUpdate::Main() {
  HRESULT hr = E_FAIL;
  if (!ParseCommandLine(::GetCommandLine(), &hr)) {
    // This was either /RegServer or /UnregServer. Return early.
    return hr;
  }

  hr = InitializeServerSecurity(is_machine_);
  if (FAILED(hr)) {
    return hr;
  }

  DisableCOMExceptionHandling();

  // TODO(omaha3): We do not call worker_->Run() from anywhere. This means that
  // the ThreadPool and the ShutdownHandler within the Worker are not
  // initialized. We need to eventually fix this.

  CORE_LOG(L2, (_T("[Calling CAtlExeModuleT<GoogleUpdate>::WinMain]")));
  return CAtlExeModuleT<GoogleUpdate>::WinMain(0);
}

HRESULT GoogleUpdate::RegisterClassObjects(DWORD, DWORD) throw() {
  CORE_LOG(L3, (_T("[RegisterClassObjects]")));

  for (_ATL_OBJMAP_ENTRY* entry = GetObjectMap();
       entry && entry->pclsid != NULL;
       entry++) {
    HRESULT hr = entry->RegisterClassObject(CLSCTX_LOCAL_SERVER,
                                            REGCLS_MULTIPLEUSE |
                                            REGCLS_SUSPENDED);
    if (FAILED(hr)) {
      CORE_LOG(LE, (_T("[RegisterClassObject failed][%s][0x%x]"),
                    GuidToString(*entry->pclsid), hr));
      return hr;
    }
  }

  return S_OK;
}

HRESULT GoogleUpdate::RevokeClassObjects() throw() {
  CORE_LOG(L3, (_T("[RevokeClassObjects]")));

  for (_ATL_OBJMAP_ENTRY* entry = GetObjectMap();
       entry && entry->pclsid != NULL;
       entry++) {
    HRESULT hr = entry->RevokeClassObject();
    if (FAILED(hr)) {
      CORE_LOG(LE, (_T("[RevokeClassObject failed][%s][0x%x]"),
                    GuidToString(*entry->pclsid), hr));
      return hr;
    }
  }

  return S_OK;
}

HRESULT GoogleUpdate::RegisterOrUnregisterExe(bool is_register) {
  CORE_LOG(L3, (_T("[RegisterOrUnregisterExe][%d]"), is_register));

  for (_ATL_OBJMAP_ENTRY* entry = GetObjectMap();
       entry && entry->pclsid != NULL;
       entry++) {
    HRESULT hr = entry->pfnUpdateRegistry(is_register);
    if (FAILED(hr)) {
      CORE_LOG(LE, (_T("[pfnUpdateRegistry failed][%d][0x%x][%s]"),
                    is_register, hr, GuidToString(*entry->pclsid)));
      return hr;
    }
  }

  return S_OK;
}

HRESULT GoogleUpdate::RegisterOrUnregisterExe(void* data,
                                              bool is_register) {
  ASSERT1(data);
  return reinterpret_cast<GoogleUpdate*>(data)->RegisterOrUnregisterExe(
      is_register);
}

HRESULT RegisterOrUnregisterProxies(void* data, bool is_register) {
  ASSERT1(data);
  bool is_machine = *reinterpret_cast<bool*>(data);
  CORE_LOG(L3, (_T("[RegisterOrUnregisterProxies][%d][%d]"),
                is_machine, is_register));

  CPath ps_dll(app_util::GetCurrentModuleDirectory());
  if (!ps_dll.Append(is_machine ? kPSFileNameMachine : kPSFileNameUser)) {
    return HRESULTFromLastError();
  }

  ASSERT1(!is_register || ps_dll.FileExists());
  HRESULT hr = is_register ? RegisterDll(ps_dll) : UnregisterDll(ps_dll);
  CORE_LOG(L3, (_T("[  PS][%s][0x%x]"), ps_dll, hr));
  if (FAILED(hr) && is_register) {
    return hr;
  }

  return S_OK;
}

HRESULT GoogleUpdate::RegisterServer(BOOL, const CLSID*) throw() {
  HRESULT hr = goopdate_utils::RegisterOrUnregisterModule(
      is_machine_, true, &RegisterOrUnregisterProxies, &is_machine_);
  if (FAILED(hr)) {
    return hr;
  }

  return goopdate_utils::RegisterOrUnregisterModule(
      is_machine_,
      true,
      &GoogleUpdate::RegisterOrUnregisterExe,
      this);
}

HRESULT GoogleUpdate::UnregisterServer(BOOL, const CLSID*) throw() {
  HRESULT hr = goopdate_utils::RegisterOrUnregisterModule(
      is_machine_, false, &GoogleUpdate::RegisterOrUnregisterExe, this);
  if (FAILED(hr)) {
    return hr;
  }

  return goopdate_utils::RegisterOrUnregisterModule(
      is_machine_, false, &RegisterOrUnregisterProxies, &is_machine_);
}

HRESULT GoogleUpdate::PreMessageLoop(int show_cmd) throw() {
  return CAtlExeModuleT<GoogleUpdate>::PreMessageLoop(show_cmd);
}

HRESULT GoogleUpdate::PostMessageLoop() throw() {
  return CAtlExeModuleT<GoogleUpdate>::PostMessageLoop();
}

}  // namespace omaha
