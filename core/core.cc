// Copyright 2007-2010 Google Inc.
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

// Core is the long-lived Omaha process. It runs one instance for the
// machine and one instance for each user session, including console and TS
// sessions.
// If the same user is logged in multiple times, only one core process will
// be running.

#include "omaha/core/core.h"
#include <lmsname.h>
#include <atlsecurity.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "omaha/base/app_util.h"
#include "omaha/base/const_object_names.h"
#include "omaha/base/debug.h"
#include "omaha/base/error.h"
#include "omaha/base/logging.h"
#include "omaha/base/module_utils.h"
#include "omaha/base/path.h"
#include "omaha/base/program_instance.h"
#include "omaha/base/reactor.h"
#include "omaha/base/reg_key.h"
#include "omaha/base/service_utils.h"
#include "omaha/base/shutdown_handler.h"
#include "omaha/base/system.h"
#include "omaha/base/time.h"
#include "omaha/base/user_info.h"
#include "omaha/base/utils.h"
#include "omaha/base/vistautil.h"
#include "omaha/common/app_registry_utils.h"
#include "omaha/common/const_cmd_line.h"
#include "omaha/common/command_line_builder.h"
#include "omaha/common/config_manager.h"
#include "omaha/common/oem_install_utils.h"
#include "omaha/common/scheduled_task_utils.h"
#include "omaha/common/stats_uploader.h"
#include "omaha/core/core_metrics.h"
#include "omaha/core/scheduler.h"
#include "omaha/core/system_monitor.h"
#include "omaha/goopdate/resource_manager.h"
#include "omaha/goopdate/worker.h"
#include "omaha/net/network_config.h"
#include "omaha/setup/setup_service.h"

namespace omaha {

Core::Core()
    : is_system_(false),
      is_crash_handler_enabled_(false),
      main_thread_id_(0) {
  CORE_LOG(L1, (_T("[Core::Core]")));
}

Core::~Core() {
  CORE_LOG(L1, (_T("[Core::~Core]")));
  scheduler_.reset(NULL);
  system_monitor_.reset(NULL);
}

// We always return S_OK, because the core can be invoked from the system
// scheduler, and the scheduler does not work well if the process returns
// an error. We do not depend on the return values from the Core elsewhere.
HRESULT Core::Main(bool is_system, bool is_crash_handler_enabled) {
  HRESULT hr = DoMain(is_system, is_crash_handler_enabled);
  if (FAILED(hr)) {
    OPT_LOG(LW, (_T("[Core::DoMain failed][0x%x]"), hr));
  }

  return S_OK;
}

bool Core::AreScheduledTasksHealthy() const {
  if (!ServiceUtils::IsServiceRunning(SERVICE_SCHEDULE)) {
    ++metric_core_run_task_scheduler_not_running;
    CORE_LOG(LE, (_T("[Task Scheduler Service is not running]")));
    return false;
  }

  if (!scheduled_task_utils::IsInstalledGoopdateTaskUA(is_system_)) {
    ++metric_core_run_scheduled_task_missing;
    CORE_LOG(LE, (_T("[UA Task not installed]")));
    return false;
  }

  if (scheduled_task_utils::IsDisabledGoopdateTaskUA(is_system_)) {
    ++metric_core_run_scheduled_task_disabled;
    CORE_LOG(LE, (_T("[UA Task disabled]")));
    return false;
  }

  HRESULT ua_task_last_exit_code =
      scheduled_task_utils::GetExitCodeGoopdateTaskUA(is_system_);

  if (ua_task_last_exit_code == SCHED_S_TASK_HAS_NOT_RUN &&
      !ConfigManager::Is24HoursSinceInstall(is_system_)) {
    // Not 24 hours yet since install or update. Let us give the UA task the
    // benefit of the doubt, and assume all is well for right now.
    CORE_LOG(L3, (_T("[Core::AreScheduledTasksHealthy]")
                  _T("[Not yet 24 hours since install/update]")));
    ua_task_last_exit_code = S_OK;
  }

  metric_core_run_scheduled_task_exit_code = ua_task_last_exit_code;

  if (S_OK != ua_task_last_exit_code) {
    CORE_LOG(LE, (_T("[UA Task exit code][0x%x]"), ua_task_last_exit_code));
    return false;
  }

  return true;
}

bool Core::IsCheckingForUpdates() const {
  if (!ConfigManager::Is24HoursSinceInstall(is_system_)) {
    CORE_LOG(L3, (_T("[Core::IsCheckingForUpdates]")
                  _T("[Not yet 24 hours since install/update]")));
    return true;
  }

  const ConfigManager& cm = *ConfigManager::Instance();
  const int k14DaysSec = 14 * 24 * 60 * 60;

  if (cm.GetTimeSinceLastCheckedSec(is_system_) >= k14DaysSec) {
    ++metric_core_run_not_checking_for_updates;
    CORE_LOG(LE, (_T("[LastChecked older than 14 days]")));
    return false;
  }

  return true;
}

// The Core will run all the time under the following conditions:
//
// * the task scheduler is not running, or
// * the UA task is not installed, or
// * the UA task is disabled, or
// * the last exit code for the UA task is non-zero, or
// * LastChecked time is older than 14 days.
//
// Under these conditions, Omaha uses the built-in scheduler hosted by the core
// and it keeps the core running.
bool Core::ShouldRunForever() const {
  CORE_LOG(L3, (_T("[Core::ShouldRunForever]")));

  // The methods are being called individually to enable metrics capture.
  bool are_scheduled_tasks_healthy(AreScheduledTasksHealthy());
  bool is_checking_for_updates(IsCheckingForUpdates());

  bool result = !are_scheduled_tasks_healthy ||
                !is_checking_for_updates;
  CORE_LOG(L1, (_T("[Core::ShouldRunForever][%u]"), result));
  return result;
}


HRESULT Core::DoMain(bool is_system, bool is_crash_handler_enabled) {
  main_thread_id_ = ::GetCurrentThreadId();
  is_system_ = is_system;
  is_crash_handler_enabled_ = is_crash_handler_enabled;

  CORE_LOG(L1, (_T("[is_system_: %d][is_crash_handler_enabled_: %d]"),
                is_system_, is_crash_handler_enabled_));

  const ConfigManager& cm = *ConfigManager::Instance();
  if (oem_install_utils::IsOemInstalling(is_system_)) {
    // Exit immediately while an OEM is installing Windows. This prevents cores
    // or update workers from being started by the Scheduled Task or other means
    // before the system is sealed.
    OPT_LOG(L1, (_T("[Exiting because an OEM is installing Windows]")));
    ASSERT1(is_system_);
    return S_OK;
  }

  // Do a code red check as soon as possible.
  StartCodeRed();

  CORE_LOG(L2, (_T("[IsInternalUser: %d]"), cm.IsInternalUser()));

  NamedObjectAttributes single_core_attr;
  GetNamedObjectAttributes(kCoreSingleInstance, is_system_, &single_core_attr);
  ProgramInstance instance(single_core_attr.name);
  bool is_already_running = !instance.EnsureSingleInstance();
  if (is_already_running) {
    OPT_LOG(L1, (_T("[Another core instance is already running]")));
    return S_OK;
  }

  // TODO(omaha): the user Omaha core should run at medium integrity level and
  // it should deelevate itself if it does not, see bug 1549842.

  // Start the crash handler if necessary.
  if (is_crash_handler_enabled_) {
    HRESULT hr = StartCrashHandler();
    if (FAILED(hr)) {
      OPT_LOG(LW, (_T("[Failed to start crash handler][0x%08x]"), hr));
    }
  }

  if (!ShouldRunForever()) {
    return S_OK;
  }

  // TODO(omaha): Delay starting update worker when run at startup.
  StartUpdateWorkerInternal();

  // Force the main thread to create a message queue so any future WM_QUIT
  // message posted by the ShutdownHandler will be received. If the main
  // thread does not have a message queue, the message can be lost.
  MSG msg = {0};
  ::PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

  reactor_.reset(new Reactor);
  shutdown_handler_.reset(new ShutdownHandler);
  HRESULT hr = shutdown_handler_->Initialize(reactor_.get(), this, is_system_);
  if (FAILED(hr)) {
    return hr;
  }

  scheduler_.reset(new Scheduler(*this));
  hr = scheduler_->Initialize();
  if (FAILED(hr)) {
    return hr;
  }

  system_monitor_.reset(new SystemMonitor(is_system_));
  VERIFY1(SUCCEEDED(system_monitor_->Initialize(true)));
  system_monitor_->set_observer(this);

  // Start processing messages and events from the system.
  return DoRun();
}

// Signals the core to shutdown. The shutdown method is called by a thread
// running in the thread pool. It posts a WM_QUIT to the main thread, which
// causes it to break out of the message loop. If the message can't be posted,
// it terminates the process unconditionally.
HRESULT Core::Shutdown() {
  return ShutdownInternal();
}

HRESULT Core::ShutdownInternal() const {
  LONG atl_module_count(const_cast<Core*>(this)->GetLockCount());
  if (atl_module_count > 0) {
    CORE_LOG(L1, (_T("[Core COM server in use][%d]"), atl_module_count));
    return S_OK;
  }

  OPT_LOG(L1, (_T("[Google Update core is shutting down...]")));
  ASSERT1(::GetCurrentThreadId() != main_thread_id_);
  if (::PostThreadMessage(main_thread_id_, WM_QUIT, 0, 0)) {
    return S_OK;
  }

  ASSERT(false, (_T("Failed to post WM_QUIT")));
  uint32 exit_code = static_cast<uint32>(E_ABORT);
  VERIFY1(::TerminateProcess(::GetCurrentProcess(), exit_code));
  return S_OK;
}

void Core::LastCheckedDeleted() {
  OPT_LOG(L1, (_T("[Core::LastCheckedDeleted]")));
  VERIFY1(SUCCEEDED(StartUpdateWorker()));
}

void Core::NoRegisteredClients() {
  OPT_LOG(L1, (_T("[Core::NoRegisteredClients]")));
  VERIFY1(SUCCEEDED(StartUpdateWorker()));
}

HRESULT Core::DoRun() {
  OPT_LOG(L1, (_T("[Core::DoRun]")));

  // Trim the process working set to minimum. It does not need a more complex
  // algorithm for now. Likely the working set will increase slightly over time
  // as the core is handling events.
  VERIFY1(::SetProcessWorkingSetSize(::GetCurrentProcess(),
                                     static_cast<uint32>(-1),
                                     static_cast<uint32>(-1)));
  return DoHandleEvents();
}

HRESULT Core::DoHandleEvents() {
  CORE_LOG(L1, (_T("[Core::DoHandleEvents]")));
  MSG msg = {0};
  int result = 0;
  while ((result = ::GetMessage(&msg, 0, 0, 0)) != 0) {
    ::DispatchMessage(&msg);
    if (result == -1) {
      break;
    }
  }
  CORE_LOG(L3, (_T("[GetMessage returned %d]"), result));
  return (result != -1) ? S_OK : HRESULTFromLastError();
}

HRESULT Core::StartUpdateWorker() const {
  if (!ShouldRunForever()) {
    return ShutdownInternal();
  }

  return StartUpdateWorkerInternal();
}

HRESULT Core::StartUpdateWorkerInternal() const {
  CORE_LOG(L2, (_T("[Core::StartUpdateWorkerInternal]")));

  CString exe_path = goopdate_utils::BuildGoogleUpdateExePath(is_system_);
  CommandLineBuilder builder(COMMANDLINE_MODE_UA);
  builder.set_install_source(kCmdLineInstallSource_Core);
  CString cmd_line = builder.GetCommandLineArgs();
  HRESULT hr = System::StartProcessWithArgs(exe_path, cmd_line);
  if (SUCCEEDED(hr)) {
    ++metric_core_worker_succeeded;
  } else {
    CORE_LOG(LE, (_T("[can't start update worker][0x%08x]"), hr));
  }
  ++metric_core_worker_total;
  return hr;
}

HRESULT Core::StartCodeRed() const {
  if (RegKey::HasValue(MACHINE_REG_UPDATE_DEV, kRegValueNoCodeRedCheck)) {
    CORE_LOG(LW, (_T("[Code Red is disabled for this system]")));
    return E_ABORT;
  }

  CORE_LOG(L2, (_T("[Core::StartCodeRed]")));

  CString exe_path = goopdate_utils::BuildGoogleUpdateExePath(is_system_);
  CommandLineBuilder builder(COMMANDLINE_MODE_CODE_RED_CHECK);
  CString cmd_line = builder.GetCommandLineArgs();
  HRESULT hr = System::StartProcessWithArgs(exe_path, cmd_line);
  if (SUCCEEDED(hr)) {
    ++metric_core_cr_succeeded;
  } else {
    CORE_LOG(LE, (_T("[can't start Code Red worker][0x%08x]"), hr));
  }
  ++metric_core_cr_total;
  return hr;
}

HRESULT Core::StartCrashHandler() const {
  CORE_LOG(L2, (_T("[Core::StartCrashHandler]")));

  HRESULT hr = goopdate_utils::StartCrashHandler(is_system_);
  if (SUCCEEDED(hr)) {
    ++metric_core_start_crash_handler_succeeded;
  } else {
    CORE_LOG(LE, (_T("[Cannot start Crash Handler][0x%08x]"), hr));
  }
  ++metric_core_start_crash_handler_total;
  return hr;
}

void Core::AggregateMetrics() const {
  CORE_LOG(L2, (_T("[aggregate core metrics]")));
  CollectMetrics();
  VERIFY1(SUCCEEDED(omaha::AggregateMetrics(is_system_)));
}

// Collects: working set, peak working set, handle count, process uptime,
// user disk free space on the current drive, process kernel time, and process
// user time.
void Core::CollectMetrics() const {
  uint64 working_set(0), peak_working_set(0);
  VERIFY1(SUCCEEDED(System::GetProcessMemoryStatistics(&working_set,
                                                       &peak_working_set,
                                                       NULL,
                                                       NULL)));
  metric_core_working_set      = working_set;
  metric_core_peak_working_set = peak_working_set;

  metric_core_handle_count = System::GetProcessHandleCount();

  FILETIME now = {0};
  FILETIME creation_time = {0};
  FILETIME exit_time = {0};
  FILETIME kernel_time = {0};
  FILETIME user_time = {0};

  ::GetSystemTimeAsFileTime(&now);

  VERIFY1(::GetProcessTimes(::GetCurrentProcess(),
                            &creation_time,
                            &exit_time,
                            &kernel_time,
                            &user_time));

  ASSERT1(FileTimeToInt64(now) >= FileTimeToInt64(creation_time));
  uint64 uptime_100ns = FileTimeToInt64(now) - FileTimeToInt64(creation_time);

  metric_core_uptime_ms      = uptime_100ns / kMillisecsTo100ns;
  metric_core_kernel_time_ms = FileTimeToInt64(kernel_time) / kMillisecsTo100ns;
  metric_core_user_time_ms   = FileTimeToInt64(user_time) / kMillisecsTo100ns;

  uint64 free_bytes_current_user(0);
  uint64 total_bytes_current_user(0);
  uint64 free_bytes_all_users(0);

  CString directory_name(app_util::GetCurrentModuleDirectory());
  VERIFY1(SUCCEEDED(System::GetDiskStatistics(directory_name,
                                              &free_bytes_current_user,
                                              &total_bytes_current_user,
                                              &free_bytes_all_users)));
  metric_core_disk_space_available = free_bytes_current_user;
}

}  // namespace omaha
