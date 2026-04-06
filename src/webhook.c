#include "webhook.h"
#include <obs-module.h>
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

static void webhook_do_send(const char *url, const char *json_body)
{
	CURL *curl = curl_easy_init();
	if (!curl) {
		blog(LOG_WARNING, "[%s] Webhook: curl_easy_init failed",
		     "Easy IRL Stream");
		return;
	}

	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "easy-irl-stream-webhook/1.0");
#ifdef CURLSSLOPT_NATIVE_CA
	curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#endif

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		blog(LOG_WARNING, "[%s] Webhook failed (%s): %s",
		     "Easy IRL Stream", url, curl_easy_strerror(res));
	} else {
		long http_code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		blog(LOG_DEBUG, "[%s] Webhook sent: %s (HTTP %ld)",
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
	blog(LOG_DEBUG, "[%s] Executing command: %s", "Easy IRL Stream",
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
