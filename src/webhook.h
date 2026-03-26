#pragma once

void webhook_send_async(const char *url, const char *event_name,
			const char *source_name);

void webhook_execute_command_async(const char *command);
