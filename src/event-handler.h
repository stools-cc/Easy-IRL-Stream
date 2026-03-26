#pragma once

#include "irl-source.h"

void event_handler_on_connect(struct irl_source_data *data);
void event_handler_on_disconnect(struct irl_source_data *data);
void event_handler_tick(struct irl_source_data *data);

int64_t event_handler_get_bitrate(struct irl_source_data *data);
