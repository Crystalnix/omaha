// Copyright 2008-2009 Google Inc.
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

#include "omaha/common/webplugin_utils.h"

#include "omaha/base/app_util.h"
#include "omaha/base/debug.h"
#include "omaha/base/error.h"
#include "omaha/base/file.h"
#include "omaha/base/logging.h"
#include "omaha/base/path.h"
#include "omaha/base/safe_format.h"
#include "omaha/base/string.h"
#include "omaha/base/system.h"
#include "omaha/base/utils.h"
#include "omaha/base/xml_utils.h"
#include "omaha/common/command_line_builder.h"
#include "omaha/common/const_goopdate.h"
#include "omaha/common/goopdate_utils.h"
#include "omaha/common/lang.h"
#include "omaha/net/browser_request.h"
#include "omaha/net/cup_request.h"
#include "omaha/net/network_request.h"
#include "omaha/net/simple_request.h"

namespace omaha {

namespace webplugin_utils {

HRESULT BuildOneClickRequestString(const CommandLineArgs& args,
                                   CString* request_str) {
  if (NULL == request_str) {
    return E_INVALIDARG;
  }

  // If we're not /webplugin or the urldomain is empty, something's wrong.
  if (args.mode != COMMANDLINE_MODE_WEBPLUGIN ||
      args.webplugin_urldomain.IsEmpty()) {
    return E_UNEXPECTED;
  }

  const TCHAR* request_string_template = _T("?du=%s&args=%s");
  CString request;

  CString urldomain_escaped;
  CString pluginargs_escaped;

  StringEscape(args.webplugin_urldomain, false, &urldomain_escaped);
  StringEscape(args.webplugin_args, false, &pluginargs_escaped);

  SafeCStringFormat(&request, request_string_template,
                    urldomain_escaped,
                    pluginargs_escaped);

  *request_str = request;
  return S_OK;
}

HRESULT IsLanguageSupported(const CString& webplugin_args) {
  CString cmd_line;
  SafeCStringFormat(&cmd_line, _T("gu.exe %s"), webplugin_args);
  CommandLineArgs parsed_args;
  HRESULT hr = ParseCommandLine(cmd_line, &parsed_args);
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[ParseCommandLine failed][0x%08x]"), hr));
    return hr;
  }


  if (!lang::IsLanguageSupported(parsed_args.extra.language)) {
    CORE_LOG(LE, (_T("Language not supported][%s]"),
                  parsed_args.extra.language));
    return GOOPDATE_E_ONECLICK_LANGUAGE_NOT_SUPPORTED;
  }

  return S_OK;
}

HRESULT BuildOneClickWorkerArgs(const CommandLineArgs& args,
                                CString* oneclick_args) {
  ASSERT1(oneclick_args);

  // Since this is being called via WebPlugin only, we can rebuild the
  // command line arguments from the valid params we can send on.
  // For example, the web plugin will not send crash_cmd or debug_cmd
  // or reg_server or unreg_server so we don't have to worry about those here.
  CString cmd_line_args;
  CommandLineArgs webplugin_cmdline_args;

  // ParseCommandLine assumes the first argument is the program being run.
  // Don't want to enforce that constraint on our callers, so we prepend with a
  // fake exe name.
  CString args_to_parse;
  SafeCStringFormat(&args_to_parse, _T("%s %s"),
                    kOmahaShellFileName,
                    args.webplugin_args);

  // Parse the arguments we received as the second parameter to /webplugin.
  HRESULT hr = ParseCommandLine(args_to_parse, &webplugin_cmdline_args);
  if (FAILED(hr)) {
    return hr;
  }

  // Silent and other non-standard installs could be malicious. Prevent them.
  if (webplugin_cmdline_args.mode != COMMANDLINE_MODE_INSTALL) {
    return E_INVALIDARG;
  }
  if (webplugin_cmdline_args.is_silent_set ||
      webplugin_cmdline_args.is_eula_required_set) {
    return E_INVALIDARG;
  }

  CommandLineBuilder builder(COMMANDLINE_MODE_INSTALL);
  builder.set_extra_args(webplugin_cmdline_args.extra_args_str);

  // We expect this value from the plugin.
  ASSERT1(!args.install_source.IsEmpty());
  if (args.install_source.IsEmpty()) {
    return E_INVALIDARG;
  }

  builder.set_install_source(args.install_source);

  *oneclick_args = builder.GetCommandLineArgs();

  return S_OK;
}

// It is important that current_goopdate_path be the version path and not the
// Update\ path.
HRESULT CopyGoopdateToTempDir(const CPath& current_goopdate_path,
                              CPath* goopdate_temp_path) {
  ASSERT1(goopdate_temp_path);

  // Create a unique directory in the user's temp directory.
  TCHAR pathbuf[MAX_PATH] = {0};
  DWORD ret = ::GetTempPath(arraysize(pathbuf), pathbuf);
  if (0 == ret) {
    return HRESULTFromLastError();
  }
  if (ret >= arraysize(pathbuf)) {
    return E_FAIL;
  }

  GUID guid = GUID_NULL;
  HRESULT hr = ::CoCreateGuid(&guid);
  if (FAILED(hr)) {
    return hr;
  }

  CString guid_str = GuidToString(guid);
  CPath temp_path = pathbuf;
  temp_path.Append(guid_str);
  temp_path.Canonicalize();

  hr = CreateDir(temp_path, NULL);
  if (FAILED(hr)) {
    return hr;
  }

  hr = File::CopyTree(current_goopdate_path, temp_path, true);
  if (FAILED(hr)) {
    return hr;
  }

  CORE_LOG(L2, (_T("[CopyGoopdateToTempDir][temp_path = %s]"), temp_path));
  *goopdate_temp_path = temp_path;
  return S_OK;
}

HRESULT DoOneClickInstall(const CommandLineArgs& args) {
  CString cmd_line_args;
  HRESULT hr = BuildOneClickWorkerArgs(args, &cmd_line_args);
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[BuildOneClickWorkerArgs failed][0x%08x]"), hr));
    return hr;
  }

  CORE_LOG(L2, (_T("[DoOneClickInstall][cmd_line_args: %s]"), cmd_line_args));

  // Check if we're running from the machine dir.
  // If we're not, we must be running from user directory since OneClick only
  // works against installed versions of Omaha.
  CPath current_goopdate_path(app_util::GetCurrentModuleDirectory());
  CPath goopdate_temp_path;
  hr = CopyGoopdateToTempDir(current_goopdate_path, &goopdate_temp_path);
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[CopyGoopdateToTempDir failed][0x%08x]"), hr));
    return hr;
  }

  CPath goopdate_temp_exe_path = goopdate_temp_path;
  goopdate_temp_exe_path.Append(kOmahaShellFileName);

  // Launch goopdate again with the updated command line arguments.
  hr = System::ShellExecuteProcess(goopdate_temp_exe_path,
                                   cmd_line_args,
                                   NULL,
                                   NULL);
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[ShellExecuteProcess failed][%s][0x%08x]"),
                  goopdate_temp_exe_path, hr));
    return hr;
  }

  return S_OK;
}

}  // namespace webplugin_utils

}  // namespace omaha

