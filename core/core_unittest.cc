// Copyright 2008-2010 Google Inc.
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


#include "omaha/base/app_util.h"
#include "omaha/base/const_object_names.h"
#include "omaha/base/error.h"
#include "omaha/base/path.h"
#include "omaha/base/scoped_any.h"
#include "omaha/base/thread.h"
#include "omaha/base/time.h"
#include "omaha/base/utils.h"
#include "omaha/base/vistautil.h"
#include "omaha/common/config_manager.h"
#include "omaha/common/const_goopdate.h"
#include "omaha/common/goopdate_utils.h"
#include "omaha/common/scheduled_task_utils.h"
#include "omaha/core/core.h"
#include "omaha/setup/setup_service.h"
#include "omaha/testing/unit_test.h"

namespace omaha {

namespace {

// Runs the core on a different thread. Since the core captures the thread id
// in its constructor, the core instance must be created on this thread, not
// on the main thread.
class CoreRunner : public Runnable {
 public:
  explicit CoreRunner(bool is_machine) : is_machine_(is_machine) {}
  virtual ~CoreRunner() {}

 private:
  virtual void Run() {
    Core core;
    core.Main(is_machine_, false);         // Do not run the crash handler.
  }

  bool is_machine_;
  DISALLOW_EVIL_CONSTRUCTORS(CoreRunner);
};

}  // namespace

class CoreTest : public testing::Test {
 public:
  CoreTest() : is_machine_(false) {}

  virtual void SetUp() {
    // The Core has it's own ATL module. ATL does not like having multiple ATL
    // modules. This TestCase saves and restore the original ATL module to get
    // around ATL's limitation. This is a hack.
    original_atl_module_ = _pAtlModule;
    _pAtlModule = NULL;

    ASSERT_HRESULT_SUCCEEDED(IsSystemProcess(&is_machine_));

    ConfigManager::Instance()->SetLastCheckedTime(is_machine_, 10);

    NamedObjectAttributes attr;
    GetNamedObjectAttributes(kShutdownEvent, is_machine_, &attr);
    reset(shutdown_event_, ::CreateEvent(&attr.sa, true, false, attr.name));
    ASSERT_TRUE(shutdown_event_);
  }

  virtual void TearDown() {
     _pAtlModule = original_atl_module_;
  }

  HRESULT SignalShutdownEvent() {
    EXPECT_TRUE(valid(shutdown_event_));
    return ::SetEvent(get(shutdown_event_)) ? S_OK : HRESULTFromLastError();
  }

  HRESULT ResetShutdownEvent() {
    EXPECT_TRUE(valid(shutdown_event_));
    return ::ResetEvent(get(shutdown_event_)) ? S_OK : HRESULTFromLastError();
  }

 protected:
  bool is_machine_;
  scoped_event shutdown_event_;

  CAtlModule* original_atl_module_;
};

// Tests the core shutdown mechanism.
TEST_F(CoreTest, Shutdown) {
  // Signal existing core instances to shutdown, otherwise new instances
  // can't start.
  ASSERT_HRESULT_SUCCEEDED(SignalShutdownEvent());
  ::Sleep(0);
  ASSERT_HRESULT_SUCCEEDED(ResetShutdownEvent());

  // Start a thread to run the core, signal the core to exit, and wait a while
  // for the thread to exit. Terminate the thread if it is still running.
  Thread thread;
  CoreRunner core_runner(is_machine_);
  EXPECT_TRUE(thread.Start(&core_runner));

  // Give the core a little time to run before signaling it to exit.
  ::Sleep(100);
  EXPECT_HRESULT_SUCCEEDED(SignalShutdownEvent());
  EXPECT_TRUE(thread.WaitTillExit(2000));
  if (thread.Running()) {
    // If you see a crash here, it was likely caused by Application Verifier.
    // TODO(omaha): Is there a better way to exit? Should we wait longer?
    thread.Terminate(-1);
  }
  EXPECT_HRESULT_SUCCEEDED(ResetShutdownEvent());
}

class CoreUtilsTest : public testing::Test {
 public:
  CoreUtilsTest() : is_machine_(vista_util::IsUserAdmin()) {}

  virtual void SetUp() {
    // The Core has it's own ATL module. ATL does not like having multiple ATL
    // modules. This TestCase saves and restore the original ATL module to get
    // around ATL's limitation. This is a hack.
    original_atl_module_ = _pAtlModule;
    _pAtlModule = NULL;

    // The Core must be created after the ATL module work around.
    core_.reset(new Core);
    core_->is_system_ = is_machine_;
  }

  virtual void TearDown() {
    _pAtlModule = original_atl_module_;
  }

  bool AreScheduledTasksHealthy() {
    return core_->AreScheduledTasksHealthy();
  }

  bool IsCheckingForUpdates() {
    return core_->IsCheckingForUpdates();
  }

  static HRESULT DoInstallService(const TCHAR* service_cmd_line) {
    return SetupUpdate3Service::DoInstallService(service_cmd_line);
  }

  static HRESULT DeleteService() {
    return SetupUpdate3Service::DeleteService();
  }

  scoped_ptr<Core> core_;
  bool is_machine_;

  CAtlModule* original_atl_module_;
};

TEST_F(CoreUtilsTest, AreScheduledTasksHealthy) {
  EXPECT_SUCCEEDED(scheduled_task_utils::UninstallGoopdateTasks(is_machine_));
  EXPECT_FALSE(AreScheduledTasksHealthy());

  CString task_path = ConcatenatePath(app_util::GetCurrentModuleDirectory(),
                                      _T("LongRunningSilent.exe"));
  EXPECT_SUCCEEDED(scheduled_task_utils::InstallGoopdateTasks(task_path,
                                                              is_machine_));
  const uint32 now = Time64ToInt32(GetCurrent100NSTime());
  const int k12HourPeriodSec = 12 * 60 * 60;
  const DWORD first_install_12 = now - k12HourPeriodSec;
  EXPECT_SUCCEEDED(RegKey::SetValue(
      ConfigManager::Instance()->registry_client_state_goopdate(is_machine_),
      kRegValueInstallTimeSec,
      first_install_12));
  EXPECT_TRUE(AreScheduledTasksHealthy());

  EXPECT_SUCCEEDED(scheduled_task_utils::UninstallGoopdateTasks(is_machine_));
}

TEST_F(CoreUtilsTest, IsCheckingForUpdates) {
  const uint32 now = Time64ToInt32(GetCurrent100NSTime());
  const int k12HourPeriodSec = 12 * 60 * 60;
  const DWORD first_install_12_hours_back = now - k12HourPeriodSec;
  EXPECT_SUCCEEDED(RegKey::SetValue(
      ConfigManager::Instance()->registry_client_state_goopdate(is_machine_),
      kRegValueInstallTimeSec,
      first_install_12_hours_back));

  ConfigManager::Instance()->SetLastCheckedTime(is_machine_, 10);
  EXPECT_TRUE(IsCheckingForUpdates());

  const int k48HourPeriodSec = 48 * 60 * 60;
  const DWORD first_install_48_hours_back = now - k48HourPeriodSec;
  EXPECT_SUCCEEDED(RegKey::SetValue(
      ConfigManager::Instance()->registry_client_state_goopdate(is_machine_),
      kRegValueInstallTimeSec,
      first_install_48_hours_back));
  EXPECT_FALSE(IsCheckingForUpdates());

  EXPECT_SUCCEEDED(goopdate_utils::UpdateLastChecked(is_machine_));
  EXPECT_TRUE(IsCheckingForUpdates());

  const int k15DaysPeriodSec = 15 * 24 * 60 * 60;
  const DWORD last_checked_15_days_back = now - k15DaysPeriodSec;
  ConfigManager::Instance()->SetLastCheckedTime(is_machine_,
                                                last_checked_15_days_back);
  EXPECT_FALSE(IsCheckingForUpdates());
}

}  // namespace omaha
