#include "webhook.h"
#include <obs-module.h>
#include "debug-log.h"
#include <util/threading.h>
#include <util/platform.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <curl/curl.h>

struct webhook_args {
	char *url;
	char *json_body;
};

struct cmd_args {
	char *command;
};

static size_t discard_response(void *ptr, size_t size, size_t nmemb,
			       void *userdata)
{
	(void)ptr;
	(void)userdata;
	return size * nmemb;
}

static int webhook_curl_debug_cb(CURL *handle, curl_infotype type, char *data,
				size_t size, void *userptr)
{
	(void)handle;
	(void)userptr;

	const char *prefix;
	switch (type) {
	case CURLINFO_TEXT:
		prefix = "* ";
		break;
	case CURLINFO_SSL_DATA_IN:
	case CURLINFO_SSL_DATA_OUT:
	case CURLINFO_DATA_IN:
	case CURLINFO_DATA_OUT:
		return 0;
	case CURLINFO_HEADER_IN:
		prefix = "< ";
		break;
	case CURLINFO_HEADER_OUT:
		prefix = "> ";
		break;
	default:
		return 0;
	}

	char buf[1024];
	size_t len = size < sizeof(buf) - 1 ? size : sizeof(buf) - 1;
	memcpy(buf, data, len);
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		len--;
	buf[len] = '\0';

	dbg_log(LOG_INFO, "[Easy IRL Stream] curl: %s%s", prefix, buf);
	return 0;
}

static void webhook_do_send(const char *url, const char *json_body)
{
	CURL *curl = curl_easy_init();
	if (!curl) {
		dbg_log(LOG_WARNING, "[%s] Webhook: curl_easy_init failed",
		     "Easy IRL Stream");
		return;
	}

	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");

	char errbuf[CURL_ERROR_SIZE] = "";

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "easy-irl-stream-webhook/1.0");
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
#ifdef _WIN32
	curl_easy_setopt(curl, CURLOPT_SSLVERSION,
			 CURL_SSLVERSION_TLSv1_2 | CURL_SSLVERSION_MAX_TLSv1_2);
	curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NO_REVOKE);
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION,
			 (long)CURL_HTTP_VERSION_1_1);
	curl_easy_setopt(curl, CURLOPT_SSL_ENABLE_ALPN, 0L);
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, (long)CURL_IPRESOLVE_V4);
#endif
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, webhook_curl_debug_cb);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		dbg_log(LOG_WARNING, "[%s] Webhook failed (%s): %s (%s)",
		     "Easy IRL Stream", url, curl_easy_strerror(res),
		     errbuf[0] ? errbuf : "no details");
	} else {
		long http_code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		dbg_log(LOG_DEBUG, "[%s] Webhook sent: %s (HTTP %ld)",
		     "Easy IRL Stream", url, http_code);
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
}

static void *webhook_thread_func(void *arg)
{
	struct webhook_args *wa = arg;
	webhook_do_send(wa->url, wa->json_body);
	bfree(wa->url);
	bfree(wa->json_body);
	bfree(wa);
	return NULL;
}

static char *build_json_body(const char *event_name, const char *source_name,
			     const struct webhook_event_data *extra)
{
	char buf[1024];

	if (extra) {
		snprintf(buf, sizeof(buf),
			 "{\"event\":\"%s\",\"source\":\"%s\","
			 "\"timestamp\":%lld,"
			 "\"bitrate_kbps\":%lld,"
			 "\"uptime_sec\":%lld,"
			 "\"video_width\":%d,"
			 "\"video_height\":%d,"
			 "\"video_codec\":\"%s\"}",
			 event_name, source_name, (long long)time(NULL),
			 (long long)extra->bitrate_kbps,
			 (long long)extra->uptime_sec,
			 extra->video_width, extra->video_height,
			 extra->video_codec ? extra->video_codec : "");
	} else {
		snprintf(buf, sizeof(buf),
			 "{\"event\":\"%s\",\"source\":\"%s\","
			 "\"timestamp\":%lld}",
			 event_name, source_name, (long long)time(NULL));
	}

	return bstrdup(buf);
}

void webhook_send_async(const char *url, const char *event_name,
			const char *source_name,
			const struct webhook_event_data *extra)
{
	if (!url || !url[0])
		return;

	struct webhook_args *wa = bzalloc(sizeof(*wa));
	wa->url = bstrdup(url);
	wa->json_body = build_json_body(event_name, source_name, extra);

	pthread_t thread;
	if (pthread_create(&thread, NULL, webhook_thread_func, wa) == 0) {
		pthread_detach(thread);
	} else {
		bfree(wa->url);
		bfree(wa->json_body);
		bfree(wa);
	}
}

static void *cmd_thread_func(void *arg)
{
	struct cmd_args *ca = arg;
	dbg_log(LOG_DEBUG, "[%s] Executing command: %s", "Easy IRL Stream",
	     ca->command);
	(void)system(ca->command);
	bfree(ca->command);
	bfree(ca);
	return NULL;
}

void webhook_execute_command_async(const char *command)
{
	if (!command || !command[0])
		return;

	struct cmd_args *ca = bzalloc(sizeof(*ca));
	ca->command = bstrdup(command);

	pthread_t thread;
	if (pthread_create(&thread, NULL, cmd_thread_func, ca) == 0) {
		pthread_detach(thread);
	} else {
		bfree(ca->command);
		bfree(ca);
	}
}
