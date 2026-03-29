#pragma once

#include "irl-source.h"

/* Polling interval in seconds */
#define SETTINGS_POLL_INTERVAL_SEC 30

/* Start/stop the background sync thread for a source */
void remote_settings_start(struct irl_source_data *data);
void remote_settings_stop(struct irl_source_data *data);

/* Report available OBS scenes and sources to the API */
void remote_report_obs_info(const char *api_token);
