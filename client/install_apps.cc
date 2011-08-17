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

#include "omaha/client/install_apps.h"
#include <atlsafe.h>
#include "base/scoped_ptr.h"
#include "omaha/base/const_object_names.h"
#include "omaha/base/debug.h"
#include "omaha/base/error.h"
#include "omaha/base/logging.h"
#include "omaha/base/reactor.h"
#include "omaha/base/scope_guard.h"
#include "omaha/base/scoped_ptr_address.h"
#include "omaha/base/shutdown_callback.h"
#include "omaha/base/shutdown_handler.h"
#include "omaha/base/string.h"
#include "omaha/base/time.h"
#include "omaha/base/utils.h"
#include "omaha/base/vista_utils.h"
#include "omaha/client/bundle_creator.h"
#include "omaha/client/bundle_installer.h"
#include "omaha/client/client_metrics.h"
#include "omaha/client/client_utils.h"
#include "omaha/client/help_url_builder.h"
#include "omaha/client/install_apps_internal.h"
#include "omaha/client/install_progress_observer.h"
#include "omaha/client/resource.h"
#include "omaha/common/command_line.h"
#include "omaha/common/config_manager.h"
#include "omaha/common/const_cmd_line.h"
#include "omaha/common/goopdate_utils.h"
#include "omaha/common/lang.h"
#include "omaha/common/ping.h"
#include "omaha/common/update3_utils.h"
#include "goopdate/omaha3_idl.h"
#include "omaha/ui/progress_wnd.h"

namespace omaha {

namespace {

// Implements the UI progress window.
class SilentProgressObserver : public InstallProgressObserver {
 public:
  explicit SilentProgressObserver(BundleInstaller* installer)
      : installer_(installer) {
    ASSERT1(installer);
  }

  virtual void OnCheckingForUpdate() {
    CORE_LOG(L3, (_T("[SilentProgressObserver::OnCheckingForUpdate]")));
  }

  virtual void OnUpdateAvailable(const CString& app_name,
                                 const CString& version_string) {
    CORE_LOG(L3, (_T("[SilentProgressObserver::OnUpdateAvailable][%s][%s]"),
                  app_name, version_string));
    UNREFERENCED_PARAMETER(app_name);
    UNREFERENCED_PARAMETER(version_string);
  }

  virtual void OnWaitingToDownload(const CString& app_name) {
    CORE_LOG(L3, (_T("[SilentProgressObserver::OnWaitingToDownload][%s]"),
                  app_name));
    UNREFERENCED_PARAMETER(app_name);
  }

  virtual void OnDownloading(const CString& app_name,
                             int time_remaining_ms,
                             int pos) {
    CORE_LOG(L5, (_T("[SilentProgressObserver::OnDownloading]")
                  _T("[%s][remaining ms=%d][pos=%d]"),
                  app_name, time_remaining_ms, pos));
    UNREFERENCED_PARAMETER(app_name);
    UNREFERENCED_PARAMETER(time_remaining_ms);
    UNREFERENCED_PARAMETER(pos);
  }

  virtual void OnWaitingRetryDownload(const CString& app_name,
                                      time64 next_retry_time) {
    CORE_LOG(L5, (_T("[SilentProgressObserver::OnWaitingRetryDownload]")
                  _T("[%s][next retry time=%llu]"),
                  app_name, next_retry_time));
    UNREFERENCED_PARAMETER(app_name);
    UNREFERENCED_PARAMETER(next_retry_time);
  }

  virtual void OnWaitingToInstall(const CString& app_name,
                                  bool* can_start_install) {
    CORE_LOG(L3, (_T("[SilentProgressObserver::OnWaitingToInstall][%s]"),
                  app_name));
    ASSERT1(can_start_install);
    UNREFERENCED_PARAMETER(app_name);
    UNREFERENCED_PARAMETER(can_start_install);
  }

  virtual void OnInstalling(const CString& app_name) {
    CORE_LOG(L5, (_T("[SilentProgressObserver::OnInstalling][%s]"), app_name));
    UNREFERENCED_PARAMETER(app_name);
  }

  virtual void OnPause() {
    CORE_LOG(L3, (_T("[SilentProgressObserver::OnPause]")));
  }

  // Terminates the message loop.
  virtual void OnComplete(const ObserverCompletionInfo& observer_info) {
    CORE_LOG(L3, (_T("[SilentProgressObserver::OnComplete][%s]"),
                  observer_info.ToString()));
    UNREFERENCED_PARAMETER(observer_info);

    installer_->DoExit();
    CORE_LOG(L1, (_T("[SilentProgressObserver][DoExit() called]")));
  }

 private:
  BundleInstaller *const installer_;
};

class OnDemandEvents : public OnDemandEventsInterface {
 public:
  explicit OnDemandEvents(BundleInstaller* installer)
      : installer_(installer) {
    ASSERT1(installer);
  }
  virtual void DoClose() {
    installer_->DoClose();
  }
  virtual void DoExit() {
    installer_->DoExit();
  }

 private:
  BundleInstaller *const installer_;
};

// This ATL module is used for BundleInstaller message loop shutdown intrinsics.
// It is also needed for cases where the Update3 server COM objects are created
// inproc.
class BundleAtlModule : public CAtlExeModuleT<BundleAtlModule> {
 public:
  explicit BundleAtlModule() : allow_post_quit_(false) {
    // Disable the delay on shutdown mechanism in CAtlExeModuleT.
    m_bDelayShutdown = false;
  }
  ~BundleAtlModule() {}

  LONG Unlock() throw() {
    LONG lRet = CComGlobalsThreadModel::Decrement(&m_nLockCnt);

    if (lRet == 0 && allow_post_quit_) {
      ::PostThreadMessage(m_dwMainThreadID, WM_QUIT, 0, 0);
    }

    return lRet;
  }

  // BundleAtlModule will only post WM_QUIT if enable_quit() is called, to avoid
  // spurious WM_QUITs during bundle initialization.
  void enable_quit() {
    allow_post_quit_ = true;
  }

 private:
  bool allow_post_quit_;

  DISALLOW_COPY_AND_ASSIGN(BundleAtlModule);
};

}  // namespace

namespace internal {

bool IsBrowserRestartSupported(BrowserType browser_type) {
  return (browser_type != BROWSER_UNKNOWN &&
          browser_type != BROWSER_DEFAULT &&
          browser_type < BROWSER_MAX);
}

InstallAppsWndEvents::InstallAppsWndEvents(bool is_machine,
                                           BundleInstaller* installer,
                                           BrowserType browser_type)
    : is_machine_(is_machine),
      installer_(installer),
      browser_type_(browser_type) {
  ASSERT1(installer_);
}

void InstallAppsWndEvents::DoClose() {
  ASSERT1(installer_);
  installer_->DoClose();
}

void InstallAppsWndEvents::DoExit() {
  ASSERT1(installer_);
  installer_->DoExit();
}

void InstallAppsWndEvents::DoCancel() {
  ASSERT1(installer_);
  installer_->DoCancel();
}

// TODO(omaha3): Need to address elevated Vista installs. Since we are doing
// a handoff, we know that Omaha is installed and can do de-elevation.
// However, BuildGetHelpUrl will return an empty string right now. Best to
// just solve the general problem.
bool InstallAppsWndEvents::DoLaunchBrowser(const CString& url) {
  CORE_LOG(L2, (_T("[InstallAppsWndEvents::DoLaunchBrowser %s]"), url));
  const BrowserType browser = BROWSER_UNKNOWN == browser_type_ ?
                              BROWSER_DEFAULT :
                              browser_type_;
  return SUCCEEDED(goopdate_utils::LaunchBrowser(is_machine_, browser, url));
}

// Restarts the browser(s) and returns whether the browser was successfully
// restarted.
bool InstallAppsWndEvents::DoRestartBrowser(bool terminate_all_browsers,
                                            const std::vector<CString>& urls) {
  // UI should not trigger this call back if the browser type is unknown.
  // Instead it should ask user to restart the browser(s) manually.
  ASSERT1(IsBrowserRestartSupported(browser_type_));

  BrowserType browser = browser_type_;
  if (browser == BROWSER_DEFAULT) {
    GetDefaultBrowserType(&browser);
  }

  TerminateBrowserResult browser_res;
  TerminateBrowserResult default_res;
  if (terminate_all_browsers) {
    VERIFY1(SUCCEEDED(goopdate_utils::TerminateAllBrowsers(browser,
                                                           &browser_res,
                                                           &default_res)));
  } else {
    VERIFY1(SUCCEEDED(goopdate_utils::TerminateBrowserProcesses(browser,
                                                                &browser_res,
                                                                &default_res)));
  }

  BrowserType default_browser_type = BROWSER_UNKNOWN;
  HRESULT hr = GetDefaultBrowserType(&default_browser_type);
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[GetDefaultBrowserType failed][0x%08x]"), hr));
  }

  BrowserType browser_to_restart = BROWSER_UNKNOWN;
  if (!goopdate_utils::GetBrowserToRestart(browser,
                                           default_browser_type,
                                           browser_res,
                                           default_res,
                                           &browser_to_restart)) {
    CORE_LOG(LE, (_T("[GetBrowserToRestart returned false. Not launching.]")));
    return false;
  }
  ASSERT1(IsBrowserRestartSupported(browser_to_restart));

  bool succeeded = true;
  for (size_t i = 0; i < urls.size(); ++i) {
    succeeded &= SUCCEEDED(goopdate_utils::LaunchBrowser(is_machine_,
                                                         browser_to_restart,
                                                         urls[i]));
  }

  return succeeded;
}

// Initiates a reboot and returns whether it was iniated successfully.
bool InstallAppsWndEvents::DoReboot() {
  ASSERT(false, (_T("Not implemented.")));
  return false;
}

CString GetBundleDisplayName(IAppBundle* app_bundle) {
  if (!app_bundle) {
    return client_utils::GetDefaultBundleName();
  }

  CComBSTR bundle_name;
  HRESULT hr = app_bundle->get_displayName(&bundle_name);
  if (FAILED(hr)) {
    CORE_LOG(LW, (_T("[get_displayName failed][0x%08x]"), hr));
  }

  return (SUCCEEDED(hr) && bundle_name.Length()) ?
      CString(bundle_name) : client_utils::GetDefaultBundleName();
}

HRESULT CreateClientUI(bool is_machine,
                       BrowserType browser_type,
                       BundleInstaller* installer,
                       IAppBundle* app_bundle,
                       InstallProgressObserver** observer,
                       OmahaWndEvents** ui_sink) {
  ASSERT1(installer);
  ASSERT1(observer);
  ASSERT1(!*observer);
  ASSERT1(ui_sink);
  ASSERT1(!*ui_sink);

  scoped_ptr<ProgressWnd> progress_wnd(
      new ProgressWnd(installer->message_loop(), NULL));
  ScopeGuard destroy_window_guard = MakeObjGuard(*progress_wnd,
                                                 &ProgressWnd::DestroyWindow);

  progress_wnd->set_is_machine(is_machine);
  progress_wnd->set_bundle_name(internal::GetBundleDisplayName(app_bundle));

  HRESULT hr = progress_wnd->Initialize();
  if (FAILED(hr)) {
    return hr;
  }

  scoped_ptr<internal::InstallAppsWndEvents> progress_wnd_events(
      new internal::InstallAppsWndEvents(is_machine, installer, browser_type));
  progress_wnd->SetEventSink(progress_wnd_events.get());

  progress_wnd->Show();
  installer->SetBundleParentWindow(progress_wnd->m_hWnd);

  destroy_window_guard.Dismiss();
  *observer = progress_wnd.release();
  *ui_sink = progress_wnd_events.release();
  return S_OK;
}

// The order of construction is important because it ensures the objects are
// deleted in a safe order (objects before their dependencies).
// Any early returns before the message loop is run must call
// progress_wnd.DestroyWindow(). Errors do not need to be reported in a UI
// because they are handled further up the call stack.
HRESULT DoInstallApps(BundleInstaller* installer,
                      IAppBundle* app_bundle,
                      bool is_machine,
                      bool is_interactive,
                      bool is_update_all_apps,
                      BrowserType browser_type,
                      bool* has_ui_been_displayed) {
  CORE_LOG(L2, (_T("[DoInstallApps]")));
  ASSERT1(installer);
  ASSERT1(has_ui_been_displayed);

  scoped_ptr<InstallProgressObserver> observer;
  scoped_ptr<OmahaWndEvents> ui_sink;
  bool listen_to_shutdown_event = false;

  CComPtr<IAppBundle> app_bundle_ptr;
  app_bundle_ptr.Attach(app_bundle);

  HRESULT hr = S_OK;
  if (is_interactive) {
    hr = CreateClientUI(is_machine,
                        browser_type,
                        installer,
                        app_bundle,
                        address(observer),
                        address(ui_sink));
    if (FAILED(hr)) {
      CORE_LOG(LE, (_T("CreateClientUI failed][0x%08x]"), hr));
      return hr;
    }
    *has_ui_been_displayed = true;
  } else {
    observer.reset(new SilentProgressObserver(installer));
    if (is_update_all_apps) {
      listen_to_shutdown_event = true;
    }
  }

  hr = installer->InstallBundle(is_machine,
                                listen_to_shutdown_event,
                                app_bundle_ptr.Detach(),
                                observer.get());

  observer.reset();

  // ui_sink must be destroyed after observer and before installer.
  ui_sink.reset();

  CORE_LOG(L1, (_T("DoInstallApps returning][0x%08x]"), hr));
  return hr;
}

void HandleInstallAppsError(HRESULT error,
                            int extra_code1,
                            bool is_machine,
                            bool is_interactive,
                            bool is_eula_accepted,
                            bool is_oem_install,
                            const CString& install_source,
                            const CommandLineExtraArgs& extra_args,
                            const CString& session_id,
                            bool* has_ui_been_displayed) {
  ASSERT1(FAILED(error));
  ASSERT1(has_ui_been_displayed);

  const CString& bundle_name = extra_args.bundle_name;
  ASSERT1(!bundle_name.IsEmpty());

  CString error_text;

  switch (error) {
    case GOOPDATE_E_USER_AND_ELEVATED_WITH_UAC_ON:
      error_text.FormatMessage(IDS_USER_SHOULD_NOT_RUN_ELEVATED_WITH_UAC_ON,
                               bundle_name);
      break;
    default: {
      CString product_name;
      VERIFY1(product_name.LoadString(IDS_PRODUCT_DISPLAY_NAME));
      error_text.FormatMessage(IDS_SETUP_FAILED, product_name, error);
      break;
    }
  }

  OPT_LOG(LE, (_T("[Failed to install apps][0x%08x][%s]"), error, error_text));

  if (is_interactive && !*has_ui_been_displayed) {
    CString primary_app_id;
    if (!extra_args.apps.empty()) {
      primary_app_id = GuidToString(extra_args.apps[0].app_guid);
    }

    *has_ui_been_displayed = client_utils::DisplayError(
                                 is_machine,
                                 bundle_name,
                                 error,
                                 extra_code1,
                                 error_text,
                                 primary_app_id,
                                 extra_args.language,
                                 extra_args.installation_id,
                                 extra_args.brand_code);
  }

  if (!is_eula_accepted || is_oem_install) {
    return;
  }

  // Send an install complete ping and do not wait for the ping to be sent.
  // Since Omaha has been installed at this point, it should be able to
  // send this ping without blocking the user flow.
  Ping ping(is_machine, session_id, install_source);
  ping.LoadAppDataFromExtraArgs(extra_args);
  PingEventPtr ping_event(
      new PingEvent(PingEvent::EVENT_INSTALL_COMPLETE,
                    PingEvent::EVENT_RESULT_HANDOFF_ERROR,
                    error,
                    extra_code1));
  ping.BuildAppsPing(ping_event);
  HRESULT send_result = ping.Send(true);
  if (FAILED(send_result)) {
    CORE_LOG(LW, (_T("[Ping::Send failed][0x%x]"), send_result));
  }
}

}  // namespace internal

HRESULT UpdateAppOnDemand(bool is_machine,
                          const CString& app_id,
                          bool is_update_check_only,
                          const CString& session_id,
                          HANDLE impersonation_token,
                          HANDLE primary_token,
                          OnDemandObserver* observer) {
  CORE_LOG(L2, (_T("[UpdateAppOnDemand][%d][%s][%d]"),
                is_machine, app_id, is_update_check_only));

  const TCHAR* install_source = is_update_check_only ?
                                kCmdLineInstallSource_OnDemandCheckForUpdate :
                                kCmdLineInstallSource_OnDemandUpdate;
  CComPtr<IAppBundle> app_bundle;
  HRESULT hr = bundle_creator::CreateForOnDemand(is_machine,
                                                 app_id,
                                                 install_source,
                                                 session_id,
                                                 impersonation_token,
                                                 primary_token,
                                                 &app_bundle);
  if (SUCCEEDED(hr)) {
    BundleInstaller installer(NULL,   // No help URL for on-demand.
                              false,  // Is not update all apps.
                              is_update_check_only,
                              false);
    hr = installer.Initialize();
    if (SUCCEEDED(hr)) {
      OnDemandEvents install_events(&installer);
      observer->SetEventSink(&install_events);

      // TODO(omaha3): Listen to shutdown event during installation?
      return installer.InstallBundle(is_machine,
                                     false,
                                     app_bundle.Detach(),
                                     observer);
    }
  }

  // The observer must be notified that the bundle has completed with an error
  // since the bundle will not be processed.
  observer->OnComplete(ObserverCompletionInfo(COMPLETION_CODE_ERROR));
  return hr;
}

HRESULT InstallApps(bool is_machine,
                    bool is_interactive,
                    bool is_eula_accepted,
                    bool is_oem_install,
                    bool is_offline,
                    const CString& offline_directory,
                    const CommandLineExtraArgs& extra_args,
                    const CString& install_source,
                    const CString& session_id,
                    bool* has_ui_been_displayed) {
  CORE_LOG(L2, (_T("[InstallApps][is_machine: %u][is_interactive: %u]")
      _T("[is_eula_accepted: %u][is_oem_install: %u][is_offline: %u]")
      _T("[offline_directory: %s]"), is_machine, is_interactive,
      is_eula_accepted, is_oem_install, is_offline, offline_directory));
  ASSERT1(has_ui_been_displayed);

  BundleAtlModule atl_module;

  CComPtr<IAppBundle> app_bundle;
  HRESULT hr = bundle_creator::CreateFromCommandLine(is_machine,
                                                     is_eula_accepted,
                                                     is_offline,
                                                     offline_directory,
                                                     extra_args,
                                                     install_source,
                                                     session_id,
                                                     is_interactive,
                                                     &app_bundle);
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[bundle_creator::CreateFromCommandLine][0x%08x]"), hr));
    internal::HandleInstallAppsError(hr,
                                     0,
                                     is_machine,
                                     is_interactive,
                                     is_eula_accepted,
                                     is_oem_install,
                                     install_source,
                                     extra_args,
                                     session_id,
                                     has_ui_been_displayed);
    return hr;
  }

  BundleInstaller installer(
      new HelpUrlBuilder(is_machine,
                         extra_args.language,
                         extra_args.installation_id,
                         extra_args.brand_code),
      false,  //  is_update_all_apps
      false,  //  is_update_check_only
      internal::IsBrowserRestartSupported(extra_args.browser_type));
  hr = installer.Initialize();
  if (FAILED(hr)) {
    return hr;
  }

  atl_module.enable_quit();
  return internal::DoInstallApps(&installer,
                                 app_bundle.Detach(),
                                 is_machine,
                                 is_interactive,
                                 false,  // Is not update all apps.
                                 extra_args.browser_type,
                                 has_ui_been_displayed);
}

HRESULT UpdateAllApps(bool is_machine,
                      bool is_interactive,
                      const CString& install_source,
                      const CString& display_language,
                      const CString& session_id,
                      bool* has_ui_been_displayed) {
  CORE_LOG(L2, (_T("[UpdateAllApps][%u][%u]"), is_machine, is_interactive));
  ASSERT1(has_ui_been_displayed);

  BundleAtlModule atl_module;

  CComPtr<IAppBundle> app_bundle;
  HRESULT hr = bundle_creator::Create(is_machine,
                                      display_language,
                                      install_source,
                                      session_id,
                                      is_interactive,
                                      &app_bundle);
  if (FAILED(hr)) {
    CORE_LOG(LE, (_T("[bundle_creator::Create failed][0x%08x]"), hr));
    return hr;
  }

  BundleInstaller installer(new HelpUrlBuilder(is_machine,
                                               display_language,
                                               GUID_NULL,
                                               CString()),
                            true,   //  is_update_all_apps
                            false,  //  is_update_check_only
                            BROWSER_UNKNOWN);
  hr = installer.Initialize();
  if (FAILED(hr)) {
    return hr;
  }

  atl_module.enable_quit();
  return internal::DoInstallApps(&installer,
                                 app_bundle.Detach(),
                                 is_machine,
                                 is_interactive,
                                 true,        // Is update all apps.
                                 BROWSER_UNKNOWN,
                                 has_ui_been_displayed);
}

}  // namespace omaha
