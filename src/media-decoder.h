#pragma once

#include "irl-source.h"

bool decoder_open(struct irl_source_data *data);
void decoder_close(struct irl_source_data *data);
bool decoder_decode_packet(struct irl_source_data *data, AVPacket *pkt);
