#include "ingest-thread.h"
#include "media-decoder.h"
#include "event-handler.h"

extern void duckdns_update(const char *domain, const char *token);

#ifdef _WIN32
#include <intrin.h>
#define atomic_add_long(ptr, val) _InterlockedExchangeAdd((ptr), (val))
#else
#define atomic_add_long(ptr, val) __sync_fetch_and_add((ptr), (val))
#endif

static int interrupt_cb(void *opaque)
{
	struct irl_source_data *data = opaque;
	return !data->active ? 1 : 0;
}

static void build_url(struct irl_source_data *data, char *buf, size_t sz)
{
	pthread_mutex_lock(&data->mutex);

	if (data->protocol == PROTOCOL_RTMP) {
		const char *key =
			(data->stream_key && data->stream_key[0])
				? data->stream_key
				: "stream";
		snprintf(buf, sz, "rtmp://0.0.0.0:%d/live/%s", data->port,
			 key);
	} else {
		struct dstr url;
		dstr_init(&url);
		dstr_printf(&url, "srt://0.0.0.0:%d?mode=listener&latency=%d",
			    data->port, data->srt_latency_ms * 1000);

		if (data->srt_passphrase && data->srt_passphrase[0]) {
			size_t plen = strlen(data->srt_passphrase);
			if (plen >= 10 && plen <= 79) {
				dstr_catf(&url, "&passphrase=%s",
					  data->srt_passphrase);
			} else {
				blog(LOG_WARNING,
				     "[%s] SRT passphrase ignored: "
				     "must be 10-79 characters (got %zu)",
				     PLUGIN_NAME, plen);
			}
		}

		if (data->srt_streamid && data->srt_streamid[0])
			dstr_catf(&url, "&streamid=%s", data->srt_streamid);

		snprintf(buf, sz, "%s", url.array);
		dstr_free(&url);
	}

	pthread_mutex_unlock(&data->mutex);
}

static void *ingest_thread_func(void *arg)
{
	struct irl_source_data *data = arg;

	os_set_thread_name("easy-irl-ingest");

	/* Update DuckDNS on startup */
	pthread_mutex_lock(&data->mutex);
	if (data->duckdns_domain && data->duckdns_domain[0] &&
	    data->duckdns_token && data->duckdns_token[0]) {
		char *dd = bstrdup(data->duckdns_domain);
		char *dt = bstrdup(data->duckdns_token);
		pthread_mutex_unlock(&data->mutex);
		duckdns_update(dd, dt);
		bfree(dd);
		bfree(dt);
	} else {
		pthread_mutex_unlock(&data->mutex);
	}

	/* Start SRTLA proxy if enabled and SRT selected */
	pthread_mutex_lock(&data->mutex);
	bool start_srtla = data->srtla_enabled &&
			   data->protocol == PROTOCOL_SRT;
	int srtla_port = data->srtla_port;
	int srt_port_val = data->port;
	pthread_mutex_unlock(&data->mutex);

	if (start_srtla)
		srtla_server_start(&data->srtla, srtla_port, srt_port_val);

	while (data->active) {
		char url[1024];
		build_url(data, url, sizeof(url));

		os_atomic_set_long(&data->connection_state,
				   CONN_STATE_LISTENING);
		blog(LOG_DEBUG, "[%s] Listening: %s", PLUGIN_NAME, url);

		AVFormatContext *fmt_ctx = avformat_alloc_context();
		if (!fmt_ctx) {
			os_sleep_ms(2000);
			continue;
		}

		fmt_ctx->interrupt_callback.callback = interrupt_cb;
		fmt_ctx->interrupt_callback.opaque = data;

		AVDictionary *opts = NULL;
		if (data->protocol == PROTOCOL_RTMP)
			av_dict_set(&opts, "listen", "1", 0);
		av_dict_set(&opts, "rw_timeout", "5000000", 0);

		int ret = avformat_open_input(&fmt_ctx, url, NULL, &opts);
		av_dict_free(&opts);

		if (ret < 0) {
			avformat_free_context(fmt_ctx);
			if (!data->active)
				break;
			char errbuf[256];
			av_strerror(ret, errbuf, sizeof(errbuf));
			blog(LOG_WARNING,
			     "[%s] avformat_open_input failed: %s",
			     PLUGIN_NAME, errbuf);
			os_sleep_ms(2000);
			continue;
		}

		data->fmt_ctx = fmt_ctx;

		ret = avformat_find_stream_info(fmt_ctx, NULL);
		if (ret < 0) {
			blog(LOG_WARNING, "[%s] Could not find stream info",
			     PLUGIN_NAME);
			avformat_close_input(&data->fmt_ctx);
			data->fmt_ctx = NULL;
			if (!data->active)
				break;
			os_sleep_ms(2000);
			continue;
		}

		if (!decoder_open(data)) {
			avformat_close_input(&data->fmt_ctx);
			data->fmt_ctx = NULL;
			if (!data->active)
				break;
			os_sleep_ms(2000);
			continue;
		}

		os_atomic_set_long(&data->connection_state,
				   CONN_STATE_CONNECTED);
		data->last_frame_time_ns = os_gettime_ns();
		data->stats_connect_time_ns = os_gettime_ns();
		data->stats_total_frames = 0;
		data->stats_total_bytes = 0;
		data->dec_vid_pkt_count = 0;
		data->dec_vid_frame_count = 0;
		event_handler_on_connect(data);

		AVPacket *pkt = av_packet_alloc();

		while (data->active) {
			ret = av_read_frame(fmt_ctx, pkt);
			if (ret < 0)
				break;

			atomic_add_long(&data->bytes_window,
					(long)pkt->size);
			data->stats_total_bytes += (uint64_t)pkt->size;

			decoder_decode_packet(data, pkt);
			av_packet_unref(pkt);
		}

		av_packet_free(&pkt);
		decoder_close(data);
		avformat_close_input(&data->fmt_ctx);
		data->fmt_ctx = NULL;

		if (data->active) {
			os_atomic_set_long(&data->connection_state,
					   CONN_STATE_DISCONNECTED);
			data->stats_connect_time_ns = 0;
			event_handler_on_disconnect(data);
			os_sleep_ms(500);
		}
	}

	srtla_server_stop(&data->srtla);

	os_atomic_set_long(&data->connection_state, CONN_STATE_IDLE);
	blog(LOG_DEBUG, "[%s] Ingest thread exited", PLUGIN_NAME);
	return NULL;
}

void ingest_thread_start(struct irl_source_data *data)
{
	if (data->thread_created)
		ingest_thread_stop(data);

	data->active = true;
	data->disconnect_actions_fired = false;

	if (pthread_create(&data->ingest_thread, NULL, ingest_thread_func,
			   data) == 0) {
		data->thread_created = true;
	} else {
		blog(LOG_ERROR, "[%s] Failed to create ingest thread",
		     PLUGIN_NAME);
		data->active = false;
	}
}

void ingest_thread_stop(struct irl_source_data *data)
{
	if (!data->thread_created)
		return;

	data->active = false;

	pthread_join(data->ingest_thread, NULL);
	data->thread_created = false;

	os_atomic_set_long(&data->connection_state, CONN_STATE_IDLE);
	blog(LOG_DEBUG, "[%s] Ingest thread stopped", PLUGIN_NAME);
}
