#include "irl-source.h"
#include "ingest-thread.h"
#include "event-handler.h"

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
	return obs_module_text("SourceName");
}

static void *irl_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct irl_source_data *data = bzalloc(sizeof(*data));
	data->source = source;
	data->video_stream_idx = -1;
	data->audio_stream_idx = -1;

	pthread_mutex_init(&data->mutex, NULL);

	load_settings(data, settings);
	ingest_thread_start(data);

	if (g_irl_source_count < MAX_IRL_SOURCES)
		g_irl_sources[g_irl_source_count++] = data;

	return data;
}

static void irl_source_destroy(void *vdata)
{
	struct irl_source_data *data = vdata;

	for (int i = 0; i < g_irl_source_count; i++) {
		if (g_irl_sources[i] == data) {
			g_irl_sources[i] =
				g_irl_sources[--g_irl_source_count];
			g_irl_sources[g_irl_source_count] = NULL;
			break;
		}
	}

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

	int old_proto = data->protocol;
	int old_port = data->port;

	pthread_mutex_lock(&data->mutex);
	char *old_key = data->stream_key ? bstrdup(data->stream_key) : NULL;
	char *old_pass =
		data->srt_passphrase ? bstrdup(data->srt_passphrase) : NULL;
	int old_lat = data->srt_latency_ms;
	pthread_mutex_unlock(&data->mutex);

	load_settings(data, settings);

	bool need_restart = (data->protocol != old_proto) ||
			    (data->port != old_port);

	pthread_mutex_lock(&data->mutex);
	if (data->stream_key && old_key) {
		if (strcmp(data->stream_key, old_key) != 0)
			need_restart = true;
	} else if (data->stream_key != old_key) {
		need_restart = true;
	}
	if (data->srt_passphrase && old_pass) {
		if (strcmp(data->srt_passphrase, old_pass) != 0)
			need_restart = true;
	} else if (data->srt_passphrase != old_pass) {
		need_restart = true;
	}
	if (data->srt_latency_ms != old_lat)
		need_restart = true;
	pthread_mutex_unlock(&data->mutex);

	bfree(old_key);
	bfree(old_pass);

	if (need_restart)
		ingest_thread_start(data);
}

static void irl_source_get_defaults(obs_data_t *settings)
{
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

/* ---- properties ---- */

static void populate_scene_list(obs_property_t *list)
{
	obs_property_list_add_string(list, obs_module_text("None"), "");

	struct obs_frontend_source_list scenes = {0};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		const char *name =
			obs_source_get_name(scenes.sources.array[i]);
		obs_property_list_add_string(list, name, name);
	}
	obs_frontend_source_list_free(&scenes);
}

static bool enum_sources_cb(void *param, obs_source_t *source)
{
	obs_property_t *list = param;
	uint32_t flags = obs_source_get_output_flags(source);

	if (flags & OBS_SOURCE_VIDEO)
		obs_property_list_add_string(
			list, obs_source_get_name(source),
			obs_source_get_name(source));
	return true;
}

static void fmt_locale(char *out, size_t out_sz, const char *locale_key,
		       const char *arg1, const char *arg2)
{
	const char *tmpl = obs_module_text(locale_key);
	char buf[512];
	size_t j = 0;
	for (size_t i = 0; tmpl[i] && j < sizeof(buf) - 1; i++) {
		if (tmpl[i] == '%' && tmpl[i + 1] == '1' && arg1) {
			size_t len = strlen(arg1);
			if (j + len < sizeof(buf)) {
				memcpy(buf + j, arg1, len);
				j += len;
			}
			i++;
		} else if (tmpl[i] == '%' && tmpl[i + 1] == '2' && arg2) {
			size_t len = strlen(arg2);
			if (j + len < sizeof(buf)) {
				memcpy(buf + j, arg2, len);
				j += len;
			}
			i++;
		} else {
			buf[j++] = tmpl[i];
		}
	}
	buf[j] = '\0';
	if (j >= out_sz)
		j = out_sz - 1;
	memcpy(out, buf, j);
	out[j] = '\0';
}

static bool protocol_changed_cb(obs_properties_t *props, obs_property_t *prop,
				obs_data_t *settings)
{
	UNUSED_PARAMETER(prop);

	int protocol = (int)obs_data_get_int(settings, "protocol");
	bool is_rtmp = (protocol == PROTOCOL_RTMP);

	obs_property_set_visible(obs_properties_get(props, "stream_key"),
				 is_rtmp);
	obs_property_set_visible(obs_properties_get(props, "srt_passphrase"),
				 !is_rtmp);
	obs_property_set_visible(obs_properties_get(props, "srt_latency"),
				 !is_rtmp);

	int default_port = is_rtmp ? 1935 : 9000;
	int cur_port = (int)obs_data_get_int(settings, "port");
	if (cur_port == 1935 || cur_port == 9000)
		obs_data_set_int(settings, "port", default_port);

	int port = (int)obs_data_get_int(settings, "port");
	const char *lip = g_local_ip[0] ? g_local_ip : "?.?.?.?";
	const char *eip = g_external_ip[0]
				  ? g_external_ip
				  : obs_module_text("IpDetecting");

	const char *ddns = obs_data_get_string(settings, "duckdns_domain");
	char remote_host[256];
	if (ddns && ddns[0])
		snprintf(remote_host, sizeof(remote_host), "%s.duckdns.org",
			 ddns);
	else
		snprintf(remote_host, sizeof(remote_host), "%s", eip);

	char port_str[16];
	snprintf(port_str, sizeof(port_str), "%d", port);

	char line_wifi[128], line_mobile[128];
	char line_url_local[256], line_url_remote[256];
	char line_key[128];
	char line_fwd[256];

	fmt_locale(line_wifi, sizeof(line_wifi), "ConnInfoSameWifi", NULL,
		   NULL);
	fmt_locale(line_mobile, sizeof(line_mobile), "ConnInfoMobile", NULL,
		   NULL);

	char info[4096];
	if (is_rtmp) {
		const char *key =
			obs_data_get_string(settings, "stream_key");
		if (!key || !key[0])
			key = "stream";

		char local_url[512], remote_url[512];
		snprintf(local_url, sizeof(local_url),
			 "rtmp://%s:%s/live/%s", lip, port_str, key);
		snprintf(remote_url, sizeof(remote_url),
			 "rtmp://%s:%s/live/%s", remote_host, port_str, key);

		fmt_locale(line_fwd, sizeof(line_fwd), "ConnInfoPortForwardTcp",
			   port_str, NULL);

		snprintf(info, sizeof(info),
			 "%s\n%s\n\n%s\n%s\n%s", line_wifi, local_url,
			 line_mobile, remote_url, line_fwd);
	} else {
		const char *pass =
			obs_data_get_string(settings, "srt_passphrase");
		const char *sid =
			obs_data_get_string(settings, "srt_streamid");
		int latency =
			(int)obs_data_get_int(settings, "srt_latency");

		struct dstr params;
		dstr_init(&params);
		if (sid && sid[0])
			dstr_catf(&params, "%sstreamid=%s",
				  params.len ? "&" : "?", sid);
		if (pass && pass[0])
			dstr_catf(&params, "%spassphrase=****",
				  params.len ? "&" : "?");
		if (latency != 200)
			dstr_catf(&params, "%slatency=%d",
				  params.len ? "&" : "?", latency);

		char local_url[512], remote_url[512];
		snprintf(local_url, sizeof(local_url), "srt://%s:%s%s", lip,
			 port_str, params.array ? params.array : "");
		snprintf(remote_url, sizeof(remote_url), "srt://%s:%s%s",
			 remote_host, port_str,
			 params.array ? params.array : "");

		fmt_locale(line_fwd, sizeof(line_fwd), "ConnInfoPortForwardUdp",
			   port_str, NULL);

		bool srtla_on =
			obs_data_get_bool(settings, "srtla_enabled");
		int srtla_p =
			(int)obs_data_get_int(settings, "srtla_port");

		if (srtla_on && srtla_p > 0) {
			char srtla_port_str[16];
			snprintf(srtla_port_str, sizeof(srtla_port_str),
				 "%d", srtla_p);

			char srtla_params[256] = "";
			if (sid && sid[0])
				snprintf(srtla_params, sizeof(srtla_params),
					 "?streamid=%s", sid);

			char srtla_local[512], srtla_remote[512];
			snprintf(srtla_local, sizeof(srtla_local),
				 "srt://%s:%d%s", lip, srtla_p,
				 srtla_params);
			snprintf(srtla_remote, sizeof(srtla_remote),
				 "srt://%s:%d%s", remote_host, srtla_p,
				 srtla_params);

			char line_srtla[128], line_fwd_srtla[256];
			fmt_locale(line_srtla, sizeof(line_srtla),
				   "ConnInfoSRTLA", NULL, NULL);
			fmt_locale(line_fwd_srtla, sizeof(line_fwd_srtla),
				   "ConnInfoPortForwardUdp", srtla_port_str,
				   NULL);

			snprintf(info, sizeof(info),
				 "%s\n%s\n\n%s\n%s\n%s\n\n"
				 "%s\n%s (WLAN)\n%s (Mobil)\n%s",
				 line_wifi, local_url, line_mobile,
				 remote_url, line_fwd, line_srtla,
				 srtla_local, srtla_remote, line_fwd_srtla);
		} else {
			snprintf(info, sizeof(info), "%s\n%s\n\n%s\n%s\n%s",
				 line_wifi, local_url, line_mobile,
				 remote_url, line_fwd);
		}
		dstr_free(&params);
	}
	obs_property_set_description(
		obs_properties_get(props, "connection_info"), info);

	/* Show/hide SRT-only fields */
	obs_property_set_visible(obs_properties_get(props, "srt_streamid"),
				 !is_rtmp);
	obs_property_t *grp_srtla = obs_properties_get(props, "grp_srtla");
	if (grp_srtla)
		obs_property_set_visible(grp_srtla, !is_rtmp);

	return true;
}

static obs_properties_t *irl_source_get_properties(void *vdata)
{
	UNUSED_PARAMETER(vdata);

	obs_properties_t *props = obs_properties_create();

	/* ---- Server ---- */
	obs_properties_add_group(
		props, "grp_server", obs_module_text("GroupServer"),
		OBS_GROUP_NORMAL, obs_properties_create());

	obs_properties_t *srv = obs_property_group_content(
		obs_properties_get(props, "grp_server"));

	obs_property_t *proto = obs_properties_add_list(
		srv, "protocol", obs_module_text("Protocol"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(proto, "RTMP", PROTOCOL_RTMP);
	obs_property_list_add_int(proto, "SRT", PROTOCOL_SRT);
	obs_property_set_modified_callback(proto, protocol_changed_cb);

	obs_properties_add_int(srv, "port", obs_module_text("Port"), 1, 65535,
			       1);

	obs_properties_add_text(srv, "stream_key",
				obs_module_text("StreamKey"),
				OBS_TEXT_DEFAULT);

	obs_properties_add_text(srv, "srt_passphrase",
				obs_module_text("SRTPassphrase"),
				OBS_TEXT_PASSWORD);

	obs_property_t *sid = obs_properties_add_text(
		srv, "srt_streamid", obs_module_text("SRTStreamID"),
		OBS_TEXT_DEFAULT);
	obs_property_set_modified_callback(sid, protocol_changed_cb);

	obs_properties_add_int(srv, "srt_latency",
			       obs_module_text("SRTLatency"), 20, 8000, 10);

	obs_properties_add_text(srv, "connection_info",
				obs_module_text("ConnectionInfo"),
				OBS_TEXT_INFO);

	/* ---- SRTLA ---- */
	obs_properties_add_group(
		props, "grp_srtla", obs_module_text("GroupSRTLA"),
		OBS_GROUP_NORMAL, obs_properties_create());

	obs_properties_t *sla = obs_property_group_content(
		obs_properties_get(props, "grp_srtla"));

	obs_properties_add_bool(sla, "srtla_enabled",
				obs_module_text("SRTLAEnabled"));
	obs_properties_add_int(sla, "srtla_port",
			       obs_module_text("SRTLAPort"), 1, 65535, 1);
	obs_properties_add_text(sla, "srtla_info",
				obs_module_text("SRTLAInfo"),
				OBS_TEXT_INFO);

	/* ---- Events ---- */
	obs_properties_add_group(
		props, "grp_events", obs_module_text("GroupEvents"),
		OBS_GROUP_NORMAL, obs_properties_create());

	obs_properties_t *evt = obs_property_group_content(
		obs_properties_get(props, "grp_events"));

	obs_properties_add_int(evt, "disconnect_timeout",
			       obs_module_text("DisconnectTimeout"), 0, 60, 1);

	obs_property_t *dscene = obs_properties_add_list(
		evt, "disconnect_scene", obs_module_text("DisconnectScene"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	populate_scene_list(dscene);

	obs_property_t *rscene = obs_properties_add_list(
		evt, "reconnect_scene", obs_module_text("ReconnectScene"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	populate_scene_list(rscene);

	obs_property_t *overlay = obs_properties_add_list(
		evt, "overlay_source", obs_module_text("OverlaySource"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(overlay, obs_module_text("None"), "");
	obs_enum_sources(enum_sources_cb, overlay);

	obs_property_t *rec = obs_properties_add_list(
		evt, "recording_action", obs_module_text("RecordingAction"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(rec, obs_module_text("None"),
				  RECORDING_ACTION_NONE);
	obs_property_list_add_int(rec, obs_module_text("RecordingStart"),
				  RECORDING_ACTION_START);
	obs_property_list_add_int(rec, obs_module_text("RecordingStop"),
				  RECORDING_ACTION_STOP);

	/* ---- Low Quality ---- */
	obs_properties_add_group(
		props, "grp_quality", obs_module_text("GroupQuality"),
		OBS_GROUP_NORMAL, obs_properties_create());

	obs_properties_t *qlt = obs_property_group_content(
		obs_properties_get(props, "grp_quality"));

	obs_properties_add_bool(qlt, "low_quality_enabled",
				obs_module_text("LowQualityEnabled"));

	obs_properties_add_int(qlt, "low_quality_bitrate",
			       obs_module_text("LowQualityBitrate"), 50, 10000,
			       50);

	obs_properties_add_int(qlt, "low_quality_timeout",
			       obs_module_text("LowQualityTimeout"), 0, 30, 1);

	obs_property_t *lqscene = obs_properties_add_list(
		qlt, "low_quality_scene", obs_module_text("LowQualityScene"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	populate_scene_list(lqscene);

	obs_property_t *lqoverlay = obs_properties_add_list(
		qlt, "low_quality_overlay",
		obs_module_text("LowQualityOverlay"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(lqoverlay, obs_module_text("None"), "");
	obs_enum_sources(enum_sources_cb, lqoverlay);

	/* ---- DuckDNS ---- */
	obs_properties_add_group(
		props, "grp_duckdns", obs_module_text("GroupDuckDNS"),
		OBS_GROUP_NORMAL, obs_properties_create());

	obs_properties_t *dns = obs_property_group_content(
		obs_properties_get(props, "grp_duckdns"));

	obs_property_t *ddns_domain = obs_properties_add_text(
		dns, "duckdns_domain", obs_module_text("DuckDNSDomain"),
		OBS_TEXT_DEFAULT);
	obs_property_set_modified_callback(ddns_domain, protocol_changed_cb);

	obs_properties_add_text(dns, "duckdns_token",
				obs_module_text("DuckDNSToken"),
				OBS_TEXT_PASSWORD);

	obs_properties_add_text(dns, "duckdns_info",
				obs_module_text("DuckDNSInfo"),
				OBS_TEXT_INFO);

	/* ---- Webhook / Command ---- */
	obs_properties_add_group(
		props, "grp_webhook", obs_module_text("GroupWebhook"),
		OBS_GROUP_NORMAL, obs_properties_create());

	obs_properties_t *wh = obs_property_group_content(
		obs_properties_get(props, "grp_webhook"));

	obs_properties_add_text(wh, "webhook_url",
				obs_module_text("WebhookURL"),
				OBS_TEXT_DEFAULT);

	obs_properties_add_text(wh, "custom_command",
				obs_module_text("CustomCommand"),
				OBS_TEXT_DEFAULT);

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
