#include "remote-settings.h"
#include "ingest-thread.h"
#include "obfuscation.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/threading.h>
#include <util/platform.h>
#include <util/dstr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include "help-dialog.hpp"

/* ---- SSL error dialog (shown once per session) ---- */

static volatile bool g_ssl_error_shown = false;

struct ssl_error_ctx {
	char detail[CURL_ERROR_SIZE];
};

static void task_show_ssl_error(void *param)
{
	struct ssl_error_ctx *ctx = param;
	ssl_error_dialog_show(ctx->detail, obs_get_locale());
	free(ctx);
}

static void maybe_show_ssl_error(CURLcode res, const char *errbuf)
{
	if (res != CURLE_SSL_CONNECT_ERROR || g_ssl_error_shown)
		return;

	g_ssl_error_shown = true;
	struct ssl_error_ctx *ctx = malloc(sizeof(*ctx));
	if (ctx) {
		snprintf(ctx->detail, sizeof(ctx->detail), "%s",
			 errbuf && errbuf[0] ? errbuf : "SSL connect error");
		obs_queue_task(OBS_TASK_UI, task_show_ssl_error, ctx, false);
	}
}

/* ---- cURL helpers ---- */

struct mem_buf {
	char *data;
	size_t size;
};

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t total = size * nmemb;
	struct mem_buf *buf = (struct mem_buf *)userp;
	char *tmp = realloc(buf->data, buf->size + total + 1);
	if (!tmp) return 0;
	buf->data = tmp;
	memcpy(buf->data + buf->size, contents, total);
	buf->size += total;
	buf->data[buf->size] = '\0';
	return total;
}

static char *api_get(const char *path, const char *token)
{
	CURL *curl = curl_easy_init();
	if (!curl) return NULL;

	char url[512];
	snprintf(url, sizeof(url), "%s%s%s",
		 obf_https_prefix(), obf_stools_host(), path);

	struct curl_slist *headers = NULL;
	char auth_header[512];
	snprintf(auth_header, sizeof(auth_header),
		 obf_auth_bearer_fmt(), token);
	headers = curl_slist_append(headers, auth_header);

	char ua[128];
	snprintf(ua, sizeof(ua), "%s%s", obf_ua_prefix(), PLUGIN_VERSION);

	struct mem_buf buf = {NULL, 0};
	char errbuf[CURL_ERROR_SIZE] = "";

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, ua);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

	CURLcode res = curl_easy_perform(curl);
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		blog(LOG_WARNING, "[%s] API GET %s failed: %s (%s)",
		     PLUGIN_NAME, path, curl_easy_strerror(res),
		     errbuf[0] ? errbuf : "no details");
		maybe_show_ssl_error(res, errbuf);
		free(buf.data);
		return NULL;
	}
	if (http_code != 200) {
		blog(LOG_WARNING, "[%s] API GET %s returned HTTP %ld",
		     PLUGIN_NAME, path, http_code);
		free(buf.data);
		return NULL;
	}

	return buf.data;
}

static bool api_post(const char *path, const char *token, const char *json_body)
{
	CURL *curl = curl_easy_init();
	if (!curl) return false;

	char url[512];
	snprintf(url, sizeof(url), "%s%s%s",
		 obf_https_prefix(), obf_stools_host(), path);

	struct curl_slist *headers = NULL;
	char auth_header[512];
	snprintf(auth_header, sizeof(auth_header),
		 obf_auth_bearer_fmt(), token);
	headers = curl_slist_append(headers, auth_header);
	headers = curl_slist_append(headers, "Content-Type: application/json");

	char ua[128];
	snprintf(ua, sizeof(ua), "%s%s", obf_ua_prefix(), PLUGIN_VERSION);

	char errbuf[CURL_ERROR_SIZE] = "";

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, ua);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

	CURLcode res = curl_easy_perform(curl);
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		blog(LOG_WARNING, "[%s] API POST %s failed: %s (%s)",
		     PLUGIN_NAME, path, curl_easy_strerror(res),
		     errbuf[0] ? errbuf : "no details");
		maybe_show_ssl_error(res, errbuf);
		return false;
	}
	if (http_code != 200) {
		blog(LOG_WARNING, "[%s] API POST %s returned HTTP %ld",
		     PLUGIN_NAME, path, http_code);
		return false;
	}

	return true;
}

/* ---- Minimal JSON parser (extract string/int/bool by key) ---- */

static const char *json_find_key(const char *json, const char *key)
{
	char search[256];
	snprintf(search, sizeof(search), "\"%s\"", key);
	const char *pos = strstr(json, search);
	if (!pos) return NULL;
	pos += strlen(search);
	while (*pos == ' ' || *pos == ':') pos++;
	return pos;
}

static int json_get_int(const char *json, const char *key, int def)
{
	const char *v = json_find_key(json, key);
	if (!v) return def;
	return atoi(v);
}

static bool json_get_bool(const char *json, const char *key, bool def)
{
	const char *v = json_find_key(json, key);
	if (!v) return def;
	if (strncmp(v, "true", 4) == 0) return true;
	if (strncmp(v, "false", 5) == 0) return false;
	return def;
}

static char *json_get_string(const char *json, const char *key)
{
	const char *v = json_find_key(json, key);
	if (!v || *v != '"') return bstrdup("");
	v++;
	const char *end = strchr(v, '"');
	if (!end) return bstrdup("");
	size_t len = (size_t)(end - v);
	char *result = bmalloc(len + 1);
	memcpy(result, v, len);
	result[len] = '\0';
	return result;
}

/* ---- Apply remote settings to source ---- */

static void apply_remote_settings(struct irl_source_data *data, const char *json)
{
	int old_proto = data->protocol;
	int old_port = data->port;

	pthread_mutex_lock(&data->mutex);
	char *old_key = data->stream_key ? bstrdup(data->stream_key) : NULL;
	char *old_pass = data->srt_passphrase ? bstrdup(data->srt_passphrase) : NULL;
	int old_lat = data->srt_latency_ms;

	data->protocol = json_get_int(json, "protocol", data->protocol);
	data->port = json_get_int(json, "port", data->port);

	bfree(data->stream_key);
	data->stream_key = json_get_string(json, "streamKey");

	bfree(data->srt_passphrase);
	data->srt_passphrase = json_get_string(json, "srtPassphrase");

	bfree(data->srt_streamid);
	data->srt_streamid = json_get_string(json, "srtStreamid");

	data->srt_latency_ms = json_get_int(json, "srtLatency", data->srt_latency_ms);
	data->disconnect_timeout_sec = json_get_int(json, "disconnectTimeout", data->disconnect_timeout_sec);

	bfree(data->disconnect_scene_name);
	data->disconnect_scene_name = json_get_string(json, "disconnectScene");

	bfree(data->reconnect_scene_name);
	data->reconnect_scene_name = json_get_string(json, "reconnectScene");

	bfree(data->overlay_source_name);
	data->overlay_source_name = json_get_string(json, "overlaySource");

	data->disconnect_recording_action = json_get_int(json, "recordingAction", data->disconnect_recording_action);
	data->low_quality_enabled = json_get_bool(json, "lowQualityEnabled", data->low_quality_enabled);
	data->low_quality_bitrate_kbps = json_get_int(json, "lowQualityBitrate", data->low_quality_bitrate_kbps);
	data->low_quality_timeout_sec = json_get_int(json, "lowQualityTimeout", data->low_quality_timeout_sec);

	bfree(data->low_quality_scene_name);
	data->low_quality_scene_name = json_get_string(json, "lowQualityScene");

	bfree(data->low_quality_overlay_name);
	data->low_quality_overlay_name = json_get_string(json, "lowQualityOverlay");

	data->srtla_enabled = json_get_bool(json, "srtlaEnabled", data->srtla_enabled);
	data->srtla_port = json_get_int(json, "srtlaPort", data->srtla_port);

	data->show_watermark = !json_get_bool(json, "patreon", false);

	bfree(data->duckdns_domain);
	data->duckdns_domain = json_get_string(json, "duckdnsDomain");

	bfree(data->duckdns_token);
	data->duckdns_token = json_get_string(json, "duckdnsToken");

	bfree(data->webhook_url);
	data->webhook_url = json_get_string(json, "webhookUrl");

	bfree(data->custom_command);
	data->custom_command = json_get_string(json, "customCommand");

	/* Check if ingest needs restart */
	bool need_restart = (data->protocol != old_proto) || (data->port != old_port);
	if (data->stream_key && old_key && strcmp(data->stream_key, old_key) != 0)
		need_restart = true;
	if (data->srt_passphrase && old_pass && strcmp(data->srt_passphrase, old_pass) != 0)
		need_restart = true;
	if (data->srt_latency_ms != old_lat)
		need_restart = true;

	pthread_mutex_unlock(&data->mutex);

	bfree(old_key);
	bfree(old_pass);

	if (need_restart)
		ingest_thread_start(data);
}

/* ---- Background poll thread ---- */

static pthread_t g_settings_thread;
static volatile bool g_settings_thread_active = false;

static void *settings_poll_thread(void *arg)
{
	struct irl_source_data *data = (struct irl_source_data *)arg;

	os_set_thread_name("irl-remote-settings");

	os_sleep_ms(3000);

	while (g_settings_thread_active) {
		obs_data_t *settings = obs_source_get_settings(data->source);
		const char *api_token = obs_data_get_string(settings, "api_token");
		char *token_copy = (api_token && api_token[0]) ? bstrdup(api_token) : NULL;
		obs_data_release(settings);

		if (token_copy) {
			char *json = api_get(obf_api_settings_path(), token_copy);
			bool force_sync = false;
			if (json) {
				apply_remote_settings(data, json);
				force_sync = json_get_bool(json, "requestSync", false);
				free(json);
			}

			remote_report_obs_info(token_copy);

			if (force_sync) {
				os_sleep_ms(2000);
				remote_report_obs_info(token_copy);
			}

			bfree(token_copy);
		}

		for (int i = 0; i < SETTINGS_POLL_INTERVAL_SEC * 10 && g_settings_thread_active; i++)
			os_sleep_ms(100);
	}

	return NULL;
}

/* ---- Public API ---- */

void remote_settings_start(struct irl_source_data *data)
{
	if (g_settings_thread_active)
		return;

	g_settings_thread_active = true;
	pthread_create(&g_settings_thread, NULL, settings_poll_thread, data);
}

void remote_settings_stop(struct irl_source_data *data)
{
	UNUSED_PARAMETER(data);
	if (!g_settings_thread_active)
		return;

	g_settings_thread_active = false;
	pthread_join(g_settings_thread, NULL);
}

/* ---- Report OBS scenes/sources ---- */

static void escape_json_string(struct dstr *out, const char *str)
{
	dstr_cat(out, "\"");
	for (const char *p = str; *p; p++) {
		if (*p == '"') dstr_cat(out, "\\\"");
		else if (*p == '\\') dstr_cat(out, "\\\\");
		else if (*p == '\n') dstr_cat(out, "\\n");
		else { char c[2] = {*p, 0}; dstr_cat(out, c); }
	}
	dstr_cat(out, "\"");
}

struct src_enum_ctx {
	struct dstr *json;
	int count;
};

static bool enum_video_sources_cb(void *param, obs_source_t *source)
{
	struct src_enum_ctx *ctx = (struct src_enum_ctx *)param;
	uint32_t flags = obs_source_get_output_flags(source);
	if (flags & OBS_SOURCE_VIDEO) {
		if (ctx->count > 0) dstr_cat(ctx->json, ",");
		escape_json_string(ctx->json, obs_source_get_name(source));
		ctx->count++;
	}
	return true;
}

extern char g_local_ip[64];
extern char g_external_ip[64];

void remote_report_obs_info(const char *api_token)
{
	if (!api_token || !api_token[0])
		return;

	struct dstr json;
	dstr_init(&json);
	dstr_cat(&json, "{\"scenes\":[");

	struct obs_frontend_source_list scenes = {0};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		if (i > 0) dstr_cat(&json, ",");
		escape_json_string(&json, obs_source_get_name(scenes.sources.array[i]));
	}
	obs_frontend_source_list_free(&scenes);

	dstr_cat(&json, "],\"sources\":[");

	struct src_enum_ctx src_ctx = { &json, 0 };

	obs_enum_sources(enum_video_sources_cb, &src_ctx);

	dstr_cat(&json, "],");

	dstr_cat(&json, "\"localIp\":");
	escape_json_string(&json, g_local_ip[0] ? g_local_ip : "");
	dstr_cat(&json, ",\"externalIp\":");
	escape_json_string(&json, g_external_ip[0] ? g_external_ip : "");
	dstr_cat(&json, ",\"pluginVersion\":");
	escape_json_string(&json, PLUGIN_VERSION);
	dstr_cat(&json, "}");

	api_post(obf_api_obs_info_path(), api_token, json.array);

	dstr_free(&json);
}
