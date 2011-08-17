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

#ifndef OMAHA_UI_COMPLETE_WND_H_
#define OMAHA_UI_COMPLETE_WND_H_

#include "base/scoped_ptr.h"
#include "omaha/ui/ui.h"
#include "omaha/ui/uilib/static_ex.h"

namespace omaha {

class CompleteWndEvents : public OmahaWndEvents {
 public:
  // Launches the browser non-privileged and returns whether the browser was
  // successfully launched.
  virtual bool DoLaunchBrowser(const CString& url) = 0;
};

class CompleteWnd : public OmahaWnd {
 public:
  CompleteWnd(CMessageLoop* message_loop, HWND parent);

  virtual HRESULT Initialize();

  void SetEventSink(CompleteWndEvents* ev);

  void DisplayCompletionDialog(bool is_success,
                               const CString& text,
                               const CString& help_url);

  BEGIN_MSG_MAP(ErrorWnd)
    MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
    NOTIFY_CODE_HANDLER(NM_STATICEX, OnUrlClicked)
    COMMAND_HANDLER(IDC_CLOSE, BN_CLICKED, OnClickedButton)
    CHAIN_MSG_MAP(OmahaWnd)
  END_MSG_MAP()

 protected:
  // Constructor to override the default dialog resource ID and control classes.
  CompleteWnd(int dialog_id,
              DWORD control_classes,
              CMessageLoop* message_loop,
              HWND parent);

  // Message and command handlers.
  LRESULT OnInitDialog(UINT msg,
                       WPARAM wparam,
                       LPARAM lparam,
                       BOOL& handled);  // NOLINT
  LRESULT OnClickedButton(WORD notify_code,
                          WORD id,
                          HWND wnd_ctl,
                          BOOL& handled);   // NOLINT
  LRESULT OnUrlClicked(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);   // NOLINT

 private:
  // Handles requests to close the window. Returns true if the window is closed.
  virtual bool MaybeCloseWindow();

  HRESULT SetControlState(bool is_success);

  HRESULT ShowGetHelpLink(const CString& help_url);

  // Due to a repaint issue in StaticEx we prefer to manage their lifetime
  // very aggressively so we contain them by reference instead of value.
  scoped_ptr<StaticEx> complete_text_;
  scoped_ptr<StaticEx> get_help_text_;

  CompleteWndEvents* events_sink_;
  const DWORD control_classes_;

  DISALLOW_EVIL_CONSTRUCTORS(CompleteWnd);
};

}  // namespace omaha

#endif  // OMAHA_UI_COMPLETE_WND_H_

