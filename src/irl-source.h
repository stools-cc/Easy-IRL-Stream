#pragma once

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/threading.h>
#include <util/platform.h>
#include <util/dstr.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#include "srtla-server.h"

#define PLUGIN_NAME "Easy IRL Stream"
#define SOURCE_ID   "easy_irl_stream_source"

#include "debug-log.h"

/* IP detection globals (filled by plugin-main.c on startup) */
extern char g_local_ip[64];
extern char g_external_ip[64];

#define PROTOCOL_RTMP 0
#define PROTOCOL_SRT  1

#define RECORDING_ACTION_NONE  0
#define RECORDING_ACTION_START 1
#define RECORDING_ACTION_STOP  2

enum connection_state {
	CONN_STATE_IDLE,
	CONN_STATE_LISTENING,
	CONN_STATE_CONNECTED,
	CONN_STATE_DISCONNECTED,
};

struct irl_source_data {
	obs_source_t *source;

	/* Settings */
	int protocol;
	int port;
	char *stream_key;
	char *srt_passphrase;
	char *srt_streamid;
	int srt_latency_ms;

	/* Ingest thread */
	pthread_t ingest_thread;
	volatile bool active;
	bool thread_created;

	/* Connection state */
	volatile long connection_state;
	uint64_t last_frame_time_ns;

	/* FFmpeg decoder context (owned by ingest thread) */
	AVFormatContext *fmt_ctx;
	AVCodecContext *video_dec_ctx;
	AVCodecContext *audio_dec_ctx;
	int video_stream_idx;
	int audio_stream_idx;

	/* Pixel-format conversion */
	struct SwsContext *sws_ctx;
	int sws_width;
	int sws_height;
	enum AVPixelFormat sws_src_fmt;
	uint8_t *video_dst_data[4];
	int video_dst_linesize[4];

	/* Audio resampler */
	struct SwrContext *swr_ctx;
	int swr_sample_rate;
	int swr_channels;

	/* Bitrate tracking (written by ingest thread, read by tick) */
	volatile long bytes_window;
	uint64_t last_bitrate_check_ns;
	int64_t current_bitrate_kbps;

	/* Event handler settings: disconnect */
	int disconnect_timeout_sec;
	char *disconnect_scene_name;
	char *reconnect_scene_name;
	char *overlay_source_name;
	int disconnect_recording_action;
	bool disconnect_actions_fired;
	uint64_t disconnect_time_ns;

	/* Event handler settings: low quality */
	bool low_quality_enabled;
	int low_quality_bitrate_kbps;
	int low_quality_timeout_sec;
	char *low_quality_scene_name;
	char *low_quality_overlay_name;
	bool low_quality_active;
	bool low_quality_actions_fired;
	uint64_t low_quality_start_ns;

	/* SRTLA */
	bool srtla_enabled;
	int srtla_port;
	struct srtla_state srtla;

	/* DuckDNS */
	char *duckdns_domain;
	char *duckdns_token;

	/* Webhook / custom command */
	char *webhook_url;
	char *custom_command;

	/* Watermark (non-patreon) */
	bool show_watermark;

	/* Decoder debug counters (per-source) */
	int dec_vid_pkt_count;
	int dec_vid_frame_count;

	/* Stats (written by ingest thread, read by UI) */
	char stats_video_codec[32];
	char stats_audio_codec[32];
	char stats_video_pixfmt[32];
	int stats_video_width;
	int stats_video_height;
	int stats_audio_sample_rate;
	uint64_t stats_connect_time_ns;
	int64_t stats_total_frames;
	uint64_t stats_total_bytes;

	pthread_mutex_t mutex;
};

#define MAX_IRL_SOURCES 8
extern struct irl_source_data *g_irl_sources[MAX_IRL_SOURCES];
extern int g_irl_source_count;

extern struct obs_source_info irl_source_info;
