// Copyright 2007-2009 Google Inc.
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


#include "omaha/ui/complete_wnd.h"
#include "omaha/base/const_addresses.h"
#include "omaha/base/debug.h"
#include "omaha/base/error.h"
#include "omaha/base/logging.h"
#include "omaha/base/safe_format.h"
#include "omaha/base/omaha_version.h"
#include "omaha/base/vistautil.h"
#include "omaha/common/goopdate_utils.h"
#include "omaha/ui/ui_metrics.h"

namespace omaha {

CompleteWnd::CompleteWnd(CMessageLoop* message_loop, HWND parent)
    : OmahaWnd(IDD_PROGRESS, message_loop, parent),
      events_sink_(NULL),
      control_classes_(ICC_STANDARD_CLASSES) {
  CORE_LOG(L3, (_T("[CompleteWnd::CompleteWnd]")));
}

// dialog_id specifies the dialog resource to use.
// control_classes specifies the control classes required for dialog_id.
CompleteWnd::CompleteWnd(int dialog_id,
                         DWORD control_classes,
                         CMessageLoop* message_loop,
                         HWND parent)
    : OmahaWnd(dialog_id, message_loop, parent),
      events_sink_(NULL),
      control_classes_(control_classes | ICC_STANDARD_CLASSES) {
  CORE_LOG(L3, (_T("[CompleteWnd::CompleteWnd]")));
}

HRESULT CompleteWnd::Initialize() {
  CORE_LOG(L3, (_T("[CompleteWnd::Initialize]")));

  HRESULT hr = InitializeCommonControls(control_classes_);
  if (FAILED(hr)) {
    return hr;
  }

  return OmahaWnd::Initialize();
}

void CompleteWnd::SetEventSink(CompleteWndEvents* ev) {
  events_sink_ = ev;
  OmahaWnd::SetEventSink(events_sink_);
}

LRESULT CompleteWnd::OnInitDialog(UINT message,
                                  WPARAM w_param,
                                  LPARAM l_param,
                                  BOOL& handled) {  // NOLINT
  CORE_LOG(L3, (_T("[CompleteWnd::OnInitDialog]")));
  UNREFERENCED_PARAMETER(message);
  UNREFERENCED_PARAMETER(w_param);
  UNREFERENCED_PARAMETER(l_param);
  handled = true;

  InitializeDialog();

  SetControlAttributes(IDC_COMPLETE_TEXT, kDisabledNonButtonAttributes);
  SetControlAttributes(IDC_ERROR_TEXT, kDisabledNonButtonAttributes);
  SetControlAttributes(IDC_IMAGE, kDisabledNonButtonAttributes);
  SetControlAttributes(IDC_GET_HELP_TEXT, kDisabledNonButtonAttributes);
  SetControlAttributes(IDC_CLOSE, kDefaultActiveButtonAttributes);

  return 1;  // Let the system set the focus.
}

LRESULT CompleteWnd::OnClickedButton(WORD notify_code,
                                     WORD id,
                                     HWND wnd_ctl,
                                     BOOL& handled) {  // NOLINT
  CORE_LOG(L3, (_T("[CompleteWnd::OnClickedButton]")));
  UNREFERENCED_PARAMETER(id);
  UNREFERENCED_PARAMETER(notify_code);
  UNREFERENCED_PARAMETER(wnd_ctl);
  ASSERT1(id == IDC_CLOSE);
  ASSERT1(is_complete());
  handled = true;

  VERIFY1(SUCCEEDED(CloseWindow()));

  return 0;
}

LRESULT CompleteWnd::OnUrlClicked(int, LPNMHDR params, BOOL& handled) {  // NOLINT
  CORE_LOG(L3, (_T("[CompleteWnd::OnUrlClicked]")));
  ASSERT1(params);

  if (IDC_GET_HELP_TEXT == params->idFrom) {
    ++metric_worker_ui_get_help_click;
  }

  NMSTATICEX* notification = reinterpret_cast<NMSTATICEX*>(params);
  ASSERT1(events_sink_);
  if (events_sink_) {
    bool is_launched = events_sink_->DoLaunchBrowser(notification->action);
    // Assert that the launch succeeded because this code should not be called
    // if the launch mechanism (i.e. IProcessLauncher when running elevated) is
    // not in place. This could also fail if the default browser has been
    // uninstalled, but that is unlikely.
    ASSERT1(is_launched);
    // TODO(omaha): Consider doing something if the browser launch failed.
    // Could display a message in English saying it failed for some reason,
    // please CTRL-C this dialog, get the URL and paste it into a browser.
  }

  handled = true;
  return 1;
}

bool CompleteWnd::MaybeCloseWindow() {
  VERIFY1(SUCCEEDED(CloseWindow()));
  return true;
}

void CompleteWnd::DisplayCompletionDialog(bool is_success,
                                          const CString& text,
                                          const CString& help_url) {
  CORE_LOG(L3, (_T("[CompleteWnd::DisplayCompletionDialog]")
                _T("[success=%d][text=%s]"), is_success, text));
  ASSERT1(!text.IsEmpty());

  // FormatMessage() converts all LFs to CRLFs, which display as boxes in UI.
  // We have also observed some BITS error messages with boxes that may have
  // been caused by CRLFs.
  // To avoid boxes, convert all CRLFs to LFs, which result in line breaks.
  CString display_text = text;
  display_text.Replace(_T("\r\n"), _T("\n"));

  if (!OmahaWnd::OnComplete()) {
    return;
  }

  // It is possible for the OnComplete callback to be called multiple times.
  // Subclassing the control multiple times results in a crash, therefore
  // unsubclass the control if the control has been created and subclassed
  // before.
  if (complete_text_ != NULL) {
    // TODO(omaha3): I'm not sure this can happen in the polling model.
    ASSERT1(false);

    complete_text_->UnsubclassWindow(true);
    complete_text_.reset(NULL);
  }

  CString s;
  if (is_success) {
    VERIFY1(s.LoadString(IDS_CLOSE));
    VERIFY1(::SetWindowText(GetDlgItem(IDC_CLOSE), s));
    complete_text_.reset(new StaticEx);
    complete_text_->SubclassWindow(GetDlgItem(IDC_COMPLETE_TEXT));
    VERIFY1(::SetWindowText(GetDlgItem(IDC_COMPLETE_TEXT), text));
  } else {
    VERIFY1(s.LoadString(IDS_CLOSE));
    VERIFY1(::SetWindowText(GetDlgItem(IDC_CLOSE), s));
    complete_text_.reset(new StaticEx);
    complete_text_->SubclassWindow(GetDlgItem(IDC_ERROR_TEXT));
    VERIFY1(::SetWindowText(GetDlgItem(IDC_ERROR_TEXT), display_text));
    VERIFY1(SUCCEEDED(ShowGetHelpLink(help_url)));
  }

  VERIFY1(SUCCEEDED(SetControlState(is_success)));

  return;
}


HRESULT CompleteWnd::SetControlState(bool is_success) {
  SetControlAttributes(is_success ? IDC_COMPLETE_TEXT : IDC_ERROR_TEXT,
                       kVisibleTextAttributes);
  if (is_success) {
    SetControlAttributes(IDC_IMAGE, kVisibleImageAttributes);
  } else {
    SetControlAttributes(IDC_GET_HELP_TEXT, kVisibleTextAttributes);
  }
  SetControlAttributes(IDC_CLOSE, kDefaultActiveButtonAttributes);

  return S_OK;
}

// If help_url is empty, no link will be displayed.
HRESULT CompleteWnd::ShowGetHelpLink(const CString& help_url) {
  // If there is no event sink, clicking the URL will fail.
  ASSERT1(events_sink_);

  if (help_url.IsEmpty()) {
    return S_OK;
  }
  ASSERT1(0 == help_url.Find(_T("http://")));

  const TCHAR* const kLinkFormat = _T("<b><a=%s>%s</a></b>");
  CString display_text;
  VERIFY1(display_text.LoadString(IDS_GET_HELP_TEXT));
  CString link_string;
  SafeCStringFormat(&link_string, kLinkFormat, help_url, display_text);

  get_help_text_.reset(new StaticEx);
  get_help_text_->SubclassWindow(GetDlgItem(IDC_GET_HELP_TEXT));
  VERIFY1(::SetWindowText(GetDlgItem(IDC_GET_HELP_TEXT), link_string));

  ++metric_worker_ui_get_help_displayed;
  return S_OK;
}

}  // namespace omaha

