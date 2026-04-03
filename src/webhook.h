#pragma once

#include <stdint.h>

struct webhook_event_data {
	int64_t bitrate_kbps;
	int64_t uptime_sec;
	int video_width;
	int video_height;
	const char *video_codec;
};

void webhook_send_async(const char *url, const char *event_name,
			const char *source_name,
			const struct webhook_event_data *extra);

void webhook_execute_command_async(const char *command);
