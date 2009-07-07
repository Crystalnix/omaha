// Copyright 2006-2009 Google Inc.
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


#include <windows.h>
#include <tchar.h>

// This file implements a tiny program that puts up a MessageBox and exits.
// It's useful for generating test installation targets.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  TCHAR my_path[MAX_PATH];
  char ver_buf[2048];
  VS_FIXEDFILEINFO *ffi = NULL;
  UINT ver_len = 0;
  GetModuleFileName(hInstance, my_path, MAX_PATH);
  GetFileVersionInfo(my_path, NULL, 2048, ver_buf);
  VerQueryValue(ver_buf, _T("\\"), reinterpret_cast<LPVOID *>(&ffi), &ver_len);

  TCHAR msgbuf[256];
  _sntprintf(msgbuf, 256, _T("I am foo v. %d.%d.%d.%d!"),
    HIWORD(ffi->dwFileVersionMS),
    LOWORD(ffi->dwFileVersionMS),
    HIWORD(ffi->dwFileVersionLS),
    LOWORD(ffi->dwFileVersionLS));
  MessageBox(NULL, msgbuf, _T("foo"), MB_OK | MB_ICONINFORMATION);
  return 0;
}
