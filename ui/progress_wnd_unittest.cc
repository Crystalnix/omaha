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

// This unit test is driving the UI through its states so that we can
// visually inspect all the controls are there in their right state and
// position. To go from state to state, simply close the window on the screen.
//
// The unit test is useful for debugging UI states so different tests are
// enabled/disabled at compile time, depending what needs to be tested.

#include <windows.h>
#include "omaha/base/utils.h"
#include "omaha/client/install_progress_observer.h"
#include "omaha/testing/unit_test.h"
#include "omaha/ui/ui.h"
#include "omaha/ui/progress_wnd.h"

namespace omaha {

class UITest : public testing::Test,
               public ProgressWndEvents,
               public WaitCallbackInterface {
 protected:
  UITest() : progress_wnd_(&progress_wnd_message_loop_, NULL) {
  }

  static void SetUpTestCase() {
    message_loop_.set_message_handler(&message_handler_);
    reset(ev_, ::CreateEvent(NULL, false, false, NULL));
  }

  virtual void SetUp() {
    ASSERT_TRUE(::ResetEvent(get(ev_)));
    ASSERT_TRUE(message_loop_.RegisterWaitForSingleObject(get(ev_), this));
    /*
    progress_wnd_.SetEventSink(this);
    progress_wnd_.set_bundle_name(_T("FooBar"));
    EXPECT_SUCCEEDED(progress_wnd_.Initialize());
    EXPECT_TRUE(progress_wnd_.CenterWindow(NULL));
    progress_wnd_.SetVisible(true);
    */
  }

  virtual void TearDown() {
    message_loop_.UnregisterWait(get(ev_));
  }

  //
  // ProgressWndEvents
  //
  virtual void DoPause() {
  }

  virtual void DoResume() {
  }

  virtual void DoClose() {
    ASSERT_TRUE(::SetEvent(get(ev_)));
  }

  virtual void DoExit() {
    ASSERT_TRUE(::SetEvent(get(ev_)));
  }

  virtual void DoCancel() {
    ASSERT_TRUE(::SetEvent(get(ev_)));
  }

  virtual bool DoRestartBrowser(bool restart_all_browsers,
                                const std::vector<CString>& urls) {
    UNREFERENCED_PARAMETER(restart_all_browsers);
    UNREFERENCED_PARAMETER(urls);

    EXPECT_TRUE(::SetEvent(get(ev_)));
    return true;
  }

  virtual bool DoReboot() {
    EXPECT_TRUE(::SetEvent(get(ev_)));
    return true;
  }

  virtual bool DoLaunchBrowser(const CString&) {
    return true;
  }

  //
  // WaitCallbackInterface
  //
  virtual bool HandleSignaled(HANDLE) {
    // Makes the message pump stop.
    return false;
  }

  void FormatWindowTitle(const TCHAR* text) {
    CString title;
    progress_wnd_.GetWindowText(CStrBuf(title, 256), 256);
    CString new_title;
    new_title.Format(_T("%s - %s"), title, text);
    progress_wnd_.SetWindowText(new_title);
  }

  CompletionCodes GetBundleOverallCompletionCode(
      const ObserverCompletionInfo& observer_info) {
    return progress_wnd_.GetBundleOverallCompletionCode(observer_info);
  }

  static BasicMessageHandler message_handler_;
  static MessageLoopWithWait message_loop_;
  ProgressWnd progress_wnd_;
  CMessageLoop progress_wnd_message_loop_;
  static scoped_event ev_;
};

BasicMessageHandler UITest::message_handler_;
MessageLoopWithWait UITest::message_loop_;
scoped_event UITest::ev_;

const CompletionCodes kCompletionCodesPriority[] = {
  COMPLETION_CODE_EXIT_SILENTLY,
  COMPLETION_CODE_EXIT_SILENTLY_ON_LAUNCH_COMMAND,
  COMPLETION_CODE_SUCCESS,
  COMPLETION_CODE_LAUNCH_COMMAND,
  COMPLETION_CODE_RESTART_BROWSER_NOTICE_ONLY,
  COMPLETION_CODE_RESTART_ALL_BROWSERS_NOTICE_ONLY,
  COMPLETION_CODE_RESTART_BROWSER,
  COMPLETION_CODE_RESTART_ALL_BROWSERS,
  COMPLETION_CODE_REBOOT_NOTICE_ONLY,
  COMPLETION_CODE_REBOOT,
  COMPLETION_CODE_ERROR,
};

TEST_F(UITest, GetBundleOverallCompletionCode_BundleFailed) {
  ObserverCompletionInfo observer_info(COMPLETION_CODE_ERROR);
  AppCompletionInfo app_info;

  for (int i = 0; i < arraysize(kCompletionCodesPriority); ++i) {
    app_info.completion_code = kCompletionCodesPriority[i];
    observer_info.apps_info.push_back(app_info);
  }

  EXPECT_EQ(COMPLETION_CODE_ERROR,
            GetBundleOverallCompletionCode(observer_info));
}

TEST_F(UITest, GetBundleOverallCompletionCode_BundleSucceeded) {
  for (int i = 0; i < arraysize(kCompletionCodesPriority); ++i) {
    ObserverCompletionInfo observer_info(COMPLETION_CODE_SUCCESS);
    AppCompletionInfo app_info;

    for (int j = 0; j <= i; ++j) {
      app_info.completion_code = kCompletionCodesPriority[i];
      observer_info.apps_info.push_back(app_info);
    }

    EXPECT_EQ(kCompletionCodesPriority[i],
              GetBundleOverallCompletionCode(observer_info));
  }
}

/*
TEST_F(UITest, Initialize) {
  FormatWindowTitle(_T("Initialize"));
  message_loop_.Process();
}

TEST_F(UITest, OnWaitingToDownload) {
  FormatWindowTitle(_T("Waiting to download"));
  progress_wnd_.OnWaitingToDownload();
  message_loop_.Process();
}

TEST_F(UITest, OnDownloading1) {
  FormatWindowTitle(_T("Downloading"));
  progress_wnd_.OnDownloading(10000, 0);
  message_loop_.Process();
}

TEST_F(UITest, OnDownloading2) {
  FormatWindowTitle(_T("Downloading"));
  progress_wnd_.OnDownloading(5000, 50);
  message_loop_.Process();
}

TEST_F(UITest, OnDownloading3) {
  FormatWindowTitle(_T("Downloading"));
  progress_wnd_.OnDownloading(0, 100);
  message_loop_.Process();
}

TEST_F(UITest, OnWaitingToInstall) {
  FormatWindowTitle(_T("Waiting to install"));
  progress_wnd_.OnWaitingToInstall();
  message_loop_.Process();
}

TEST_F(UITest, OnInstall) {
  FormatWindowTitle(_T("Installing"));
  progress_wnd_.OnInstalling();
  message_loop_.Process();
}

TEST_F(UITest, OnPause) {
  FormatWindowTitle(_T("Paused"));
  progress_wnd_.OnPause();
  message_loop_.Process();
}

TEST_F(UITest, OnCompleteSuccess) {
  FormatWindowTitle(_T("Complete success"));
  progress_wnd_.OnComplete(
      COMPLETION_CODE_SUCCESS,
      _T("Thanks for installing Gears. For more information on using ")
      _T("Gears visit the ")
      _T("<a=http://www.google.com/gears/>Gears</a> web site."),
      0);
  message_loop_.Process();
}

TEST_F(UITest, OnCompleteError) {
  FormatWindowTitle(_T("Complete error"));
  progress_wnd_.OnComplete(
      COMPLETION_CODE_ERROR,
      _T("An error occured while installing Gears: an existing copy of ")
      _T("Gears is currently running. Please exit the software and ")
      _T("retry installation. For more information visit the ")
      _T("<a=http://www.google.com/gears/>Gears</a> web site."),
      11);
  message_loop_.Process();
}

TEST_F(UITest, OnCompleteRestartAllBrowsers) {
  FormatWindowTitle(_T("Restart browsers"));
  progress_wnd_.OnComplete(COMPLETION_CODE_RESTART_ALL_BROWSERS, NULL, 0);
  message_loop_.Process();
}

TEST_F(UITest, OnCompleteReboot) {
  FormatWindowTitle(_T("Reboot"));
  progress_wnd_.OnComplete(COMPLETION_CODE_REBOOT, NULL, 0);
  message_loop_.Process();
}

TEST_F(UITest, OnCompleteRestartBrowser) {
  FormatWindowTitle(_T("Restart browser"));
  progress_wnd_.OnComplete(COMPLETION_CODE_RESTART_BROWSER, NULL, 0);
  message_loop_.Process();
}

TEST_F(UITest, OnCompleteRestartAllBrowsersNoticeOnly) {
  FormatWindowTitle(_T("Restart browsers"));
  progress_wnd_.OnComplete(COMPLETION_CODE_RESTART_ALL_BROWSERS_NOTICE_ONLY,
                           NULL,
                           0);
  message_loop_.Process();
}

TEST_F(UITest, OnCompleteRebootNoticeOnly) {
  FormatWindowTitle(_T("Reboot"));
  progress_wnd_.OnComplete(COMPLETION_CODE_REBOOT_NOTICE_ONLY, NULL, 0);
  message_loop_.Process();
}

TEST_F(UITest, OnCompleteRestartBrowserNoticeOnly) {
  FormatWindowTitle(_T("Restart browser"));
  progress_wnd_.OnComplete(COMPLETION_CODE_RESTART_BROWSER_NOTICE_ONLY,
                           NULL,
                           0);
  message_loop_.Process();
}

// Test the OnComplete can be called multiple times.
TEST_F(UITest, OnMultipleCompletes) {
  FormatWindowTitle(_T("Complete success"));
  progress_wnd_.OnComplete(
      COMPLETION_CODE_SUCCESS,
      _T("Thanks for installing Gears. For more information on using ")
      _T("Gears visit the ")
      _T("<a=http://www.google.com/gears/>Gears</a> web site."),
      0);

  FormatWindowTitle(_T("Complete error"));
  progress_wnd_.OnComplete(
      COMPLETION_CODE_ERROR,
      _T("An error occured while installing Gears: an existing copy of ")
      _T("Gears is currently running. Please exit the software and ")
      _T("retry installation. For more information visit the ")
      _T("<a=http://www.google.com/gears/>Gears</a> web site."),
      0);

  progress_wnd_.OnComplete(
      COMPLETION_CODE_ERROR,
      _T("An error occured while installing Gears: an existing copy of ")
      _T("Gears is currently running. Please exit the software and ")
      _T("retry installation. For more information visit the ")
      _T("<a=http://www.google.com/gears/>Gears</a> web site."),
      0);

  FormatWindowTitle(_T("Restart browsers"));
  progress_wnd_.OnComplete(COMPLETION_CODE_RESTART_ALL_BROWSERS, NULL, 0);
  message_loop_.Process();
}
*/

}   // namespace omaha

