// Copyright 2009 Google Inc.
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
// Declarations for the MIDL-generated entry points for the proxy/stub.

#ifndef OMAHA_GOOPDATE_OMAHA3_IDL_DATAX_H_
#define OMAHA_GOOPDATE_OMAHA3_IDL_DATAX_H_

extern "C" {
  BOOL WINAPI PrxDllMain(HINSTANCE instance, DWORD reason, LPVOID res);
  STDAPI PrxDllCanUnloadNow();
  STDAPI PrxDllGetClassObject(REFCLSID refclsid, REFIID refiid, LPVOID* ptr);
  STDAPI PrxDllRegisterServer();
  STDAPI PrxDllUnregisterServer();
}

#endif  // OMAHA_GOOPDATE_OMAHA3_IDL_DATAX_H_

