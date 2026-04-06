#include "irl-source.h"
#include "ingest-thread.h"
#include "event-handler.h"
#include "remote-settings.h"
#include "obfuscation.h"
#include "translations.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

struct irl_source_data *g_irl_sources[MAX_IRL_SOURCES] = {0};
int g_irl_source_count = 0;

/* ---- helpers ---- */

static inline void safe_bfree(char **ptr)
{
	bfree(*ptr);
	*ptr = NULL;
}

static void load_settings(struct irl_source_data *data, obs_data_t *settings)
{
	pthread_mutex_lock(&data->mutex);

	data->protocol = (int)obs_data_get_int(settings, "protocol");
	data->port = (int)obs_data_get_int(settings, "port");

	safe_bfree(&data->stream_key);
	data->stream_key =
		bstrdup(obs_data_get_string(settings, "stream_key"));

	safe_bfree(&data->srt_passphrase);
	data->srt_passphrase =
		bstrdup(obs_data_get_string(settings, "srt_passphrase"));

	safe_bfree(&data->srt_streamid);
	data->srt_streamid =
		bstrdup(obs_data_get_string(settings, "srt_streamid"));

	data->srt_latency_ms =
		(int)obs_data_get_int(settings, "srt_latency");

	data->disconnect_timeout_sec =
		(int)obs_data_get_int(settings, "disconnect_timeout");

	safe_bfree(&data->disconnect_scene_name);
	data->disconnect_scene_name =
		bstrdup(obs_data_get_string(settings, "disconnect_scene"));

	safe_bfree(&data->reconnect_scene_name);
	data->reconnect_scene_name =
		bstrdup(obs_data_get_string(settings, "reconnect_scene"));

	safe_bfree(&data->overlay_source_name);
	data->overlay_source_name =
		bstrdup(obs_data_get_string(settings, "overlay_source"));

	data->disconnect_recording_action =
		(int)obs_data_get_int(settings, "recording_action");

	data->low_quality_enabled =
		obs_data_get_bool(settings, "low_quality_enabled");
	data->low_quality_bitrate_kbps =
		(int)obs_data_get_int(settings, "low_quality_bitrate");
	data->low_quality_timeout_sec =
		(int)obs_data_get_int(settings, "low_quality_timeout");

	safe_bfree(&data->low_quality_scene_name);
	data->low_quality_scene_name =
		bstrdup(obs_data_get_string(settings, "low_quality_scene"));

	safe_bfree(&data->low_quality_overlay_name);
	data->low_quality_overlay_name =
		bstrdup(obs_data_get_string(settings, "low_quality_overlay"));

	data->srtla_enabled =
		obs_data_get_bool(settings, "srtla_enabled");
	data->srtla_port =
		(int)obs_data_get_int(settings, "srtla_port");

	safe_bfree(&data->duckdns_domain);
	data->duckdns_domain =
		bstrdup(obs_data_get_string(settings, "duckdns_domain"));

	safe_bfree(&data->duckdns_token);
	data->duckdns_token =
		bstrdup(obs_data_get_string(settings, "duckdns_token"));

	safe_bfree(&data->webhook_url);
	data->webhook_url =
		bstrdup(obs_data_get_string(settings, "webhook_url"));

	safe_bfree(&data->custom_command);
	data->custom_command =
		bstrdup(obs_data_get_string(settings, "custom_command"));

	pthread_mutex_unlock(&data->mutex);
}

/* ---- source callbacks ---- */

static const char *irl_source_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return tr_source_name();
}

static void *irl_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct irl_source_data *data = bzalloc(sizeof(*data));
	data->source = source;
	data->video_stream_idx = -1;
	data->audio_stream_idx = -1;
	data->active = true;
	data->show_watermark = true;

	pthread_mutex_init(&data->mutex, NULL);

	load_settings(data, settings);
	ingest_thread_start(data);

	if (g_irl_source_count < MAX_IRL_SOURCES)
		g_irl_sources[g_irl_source_count++] = data;

	remote_settings_start(data);

	return data;
}

static void irl_source_destroy(void *vdata)
{
	struct irl_source_data *data = vdata;

	remote_settings_stop(data);

	for (int i = 0; i < g_irl_source_count; i++) {
		if (g_irl_sources[i] == data) {
			g_irl_sources[i] =
				g_irl_sources[--g_irl_source_count];
			g_irl_sources[g_irl_source_count] = NULL;
			break;
		}
	}

	data->active = false;
	ingest_thread_stop(data);

	obs_source_output_video(data->source, NULL);

	safe_bfree(&data->stream_key);
	safe_bfree(&data->srt_passphrase);
	safe_bfree(&data->srt_streamid);
	safe_bfree(&data->disconnect_scene_name);
	safe_bfree(&data->reconnect_scene_name);
	safe_bfree(&data->overlay_source_name);
	safe_bfree(&data->low_quality_scene_name);
	safe_bfree(&data->low_quality_overlay_name);
	safe_bfree(&data->duckdns_domain);
	safe_bfree(&data->duckdns_token);
	safe_bfree(&data->webhook_url);
	safe_bfree(&data->custom_command);

	pthread_mutex_destroy(&data->mutex);
	bfree(data);
}

static void irl_source_update(void *vdata, obs_data_t *settings)
{
	struct irl_source_data *data = vdata;
	UNUSED_PARAMETER(settings);
	UNUSED_PARAMETER(data);
}

static void irl_source_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "api_token", "");

	obs_data_set_default_int(settings, "protocol", PROTOCOL_RTMP);
	obs_data_set_default_int(settings, "port", 1935);
	obs_data_set_default_string(settings, "stream_key", "stream");
	obs_data_set_default_string(settings, "srt_streamid", "stream");
	obs_data_set_default_int(settings, "srt_latency", 200);
	obs_data_set_default_int(settings, "disconnect_timeout", 5);
	obs_data_set_default_int(settings, "recording_action",
				 RECORDING_ACTION_NONE);
	obs_data_set_default_bool(settings, "low_quality_enabled", false);
	obs_data_set_default_int(settings, "low_quality_bitrate", 500);
	obs_data_set_default_int(settings, "low_quality_timeout", 3);
	obs_data_set_default_bool(settings, "srtla_enabled", false);
	obs_data_set_default_int(settings, "srtla_port", 5000);
}


static bool login_button_clicked(obs_properties_t *props, obs_property_t *prop,
				 void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(prop);
	UNUSED_PARAMETER(data);

	char url[256];
	snprintf(url, sizeof(url), "%s%s%s",
		 obf_https_prefix(), obf_stools_host(),
		 obf_dash_tools_path());

#ifdef _WIN32
	ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif __APPLE__
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "open \"%s\"", url);
	system(cmd);
#else
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "xdg-open \"%s\"", url);
	system(cmd);
#endif

	return false;
}

static obs_properties_t *irl_source_get_properties(void *vdata)
{
	UNUSED_PARAMETER(vdata);

	obs_properties_t *props = obs_properties_create();

	obs_properties_add_text(props, "api_token",
				tr_api_token(),
				OBS_TEXT_PASSWORD);

	obs_properties_add_button(props, "login_button",
				  tr_login_button(),
				  login_button_clicked);

	obs_properties_add_text(props, "api_info",
				tr_api_info(),
				OBS_TEXT_INFO);

	return props;
}

static void irl_source_video_tick(void *vdata, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct irl_source_data *data = vdata;
	event_handler_tick(data);
}

/* ---- source info ---- */

struct obs_source_info irl_source_info = {
	.id = SOURCE_ID,
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO |
			OBS_SOURCE_DO_NOT_DUPLICATE,
	.get_name = irl_source_get_name,
	.create = irl_source_create,
	.destroy = irl_source_destroy,
	.update = irl_source_update,
	.get_defaults = irl_source_get_defaults,
	.get_properties = irl_source_get_properties,
	.video_tick = irl_source_video_tick,
	.icon_type = OBS_ICON_TYPE_CAMERA,
};
