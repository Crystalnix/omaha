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

// Declares the usage metrics used by the worker module.

#ifndef OMAHA_GOOPDATE_WORKER_METRICS_H__
#define OMAHA_GOOPDATE_WORKER_METRICS_H__

#include "omaha/statsreport/metrics.h"

namespace omaha {

// How many times the download manager attempted to download a file.
DECLARE_METRIC_count(worker_download_total);
// How many times the download manager successfully downloaded a file.
DECLARE_METRIC_count(worker_download_succeeded);

// How many times the package cache attempted to put the temporary file
// to the cache directory.
DECLARE_METRIC_count(worker_package_cache_put_total);
// How many times the package cache successfully copied the temporary file
// to the cache directory.
DECLARE_METRIC_count(worker_package_cache_put_succeeded);

// How many times ExecuteAndWaitForInstaller was called.
DECLARE_METRIC_count(worker_install_execute_total);
// How many times ExecuteAndWaitForInstaller was called for an MSI.
DECLARE_METRIC_count(worker_install_execute_msi_total);

// How many MSI install attempts encountered ERROR_INSTALL_ALREADY_RUNNING
// during an update.
DECLARE_METRIC_count(worker_install_msi_in_progress_detected_update);
// How many times successfully installed MSI in a retry after
// encountering ERROR_INSTALL_ALREADY_RUNNING during an update.
DECLARE_METRIC_count(worker_install_msi_in_progress_retry_succeeded_update);
// Number of retries attempted because of ERROR_INSTALL_ALREADY_RUNNING before
// succeeded during an update.
DECLARE_METRIC_integer(
    worker_install_msi_in_progress_retry_succeeded_tries_update);

// How many MSI install attempts encountered ERROR_INSTALL_ALREADY_RUNNING
// during an install.
DECLARE_METRIC_count(worker_install_msi_in_progress_detected_install);
// How many times successfully installed MSI in a retry after
// encountering ERROR_INSTALL_ALREADY_RUNNING during an install.
DECLARE_METRIC_count(worker_install_msi_in_progress_retry_succeeded_install);
// Number of retries attempted because of ERROR_INSTALL_ALREADY_RUNNING before
// succeeded during an install.
DECLARE_METRIC_integer(
    worker_install_msi_in_progress_retry_succeeded_tries_install);

// Version of the GoogleUpdate.exe shell in use.
DECLARE_METRIC_integer(worker_shell_version);

// True if Windows is installing (is in audit mode). This should never be true.
DECLARE_METRIC_bool(worker_is_windows_installing);

// True if UAC is disabled.
DECLARE_METRIC_bool(worker_is_uac_disabled);

// True if ClickOnce is disabled for the Internet zone for the current user.
DECLARE_METRIC_bool(worker_is_clickonce_disabled);

// True if a software firewall is detected.
DECLARE_METRIC_bool(worker_has_software_firewall);

// How many times the computer was on batteries when doing an update check
// for apps.
DECLARE_METRIC_count(worker_silent_update_running_on_batteries);

// How many times an update check was attempted. Does not include installs.
DECLARE_METRIC_count(worker_update_check_total);
// How many times an update check succeeded. Does not include installs.
DECLARE_METRIC_count(worker_update_check_succeeded);

// Number of apps for which update checks skipped because EULA is not accepted.
DECLARE_METRIC_integer(worker_apps_not_updated_eula);
// Number of apps for which update checks included updatedisabled because of
// update Group Policy.
DECLARE_METRIC_integer(worker_apps_not_updated_group_policy);
// Number of apps for which update checks included updatedisabled because of
// install Group Policy.
DECLARE_METRIC_integer(worker_apps_not_installed_group_policy);

// How many times Omaha did not update an app because an Omaha update was
// available at the same time. Only incremented if both an Omaha and app update
// are available in the same update check. Max one increment per update check.
DECLARE_METRIC_count(worker_skipped_app_update_for_self_update);

// How many times a self update was available.
// Note: These are updated for all update checks whereas Omaha 2 only updated
// them for auto-updates.
DECLARE_METRIC_count(worker_self_updates_available);
// How many times a self update succeeded.
DECLARE_METRIC_count(worker_self_updates_succeeded);

// How many updates have been available. Each app in an update check is counted.
// If a self-update was available, no apps are counted.
// Updates will be counted even if updates are disabled by Group Policy. This is
// a change from Omaha 2.
DECLARE_METRIC_count(worker_app_updates_available);
// How many app updates succeeded.
DECLARE_METRIC_count(worker_app_updates_succeeded);

// Number of times Omaha has received a self-update response without
// successfully updating.
DECLARE_METRIC_integer(worker_self_update_responses);
// The time (ms) since the first time Omaha received a self-update response.
// Only reported if Omaha fails to update after first such response.
DECLARE_METRIC_integer(worker_self_update_response_time_since_first_ms);

// The most significant/left half of the GUID for the app for which Omaha has
// received the most update responses without successfully updating.
DECLARE_METRIC_integer(worker_app_max_update_responses_app_high);
// Maximum number of times for any app that Omaha has received an update
// response without successfully updating.
DECLARE_METRIC_integer(worker_app_max_update_responses);
// The time (ms) since the first time Omaha received an update response for the
// app with the most update failures.
DECLARE_METRIC_integer(worker_app_max_update_responses_ms_since_first);

// Time (ms) spent in SendPing() when DoSendPing() fails.
DECLARE_METRIC_timing(ping_failed_ms);
// Time (ms) spent in SendPing() when the ping succeeds.
DECLARE_METRIC_timing(ping_succeeded_ms);

}  // namespace omaha

#endif  // OMAHA_GOOPDATE_WORKER_METRICS_H__
