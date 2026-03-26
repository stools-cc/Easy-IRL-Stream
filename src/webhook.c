#include "webhook.h"
#include <obs-module.h>
#include <util/threading.h>
#include <util/platform.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
#define sock_close    closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_INVALID (-1)
#define sock_close    close
#endif

struct webhook_args {
	char *url;
	char *event_name;
	char *source_name;
};

struct cmd_args {
	char *command;
};

static bool parse_url(const char *url, char *host, size_t host_sz,
		      char *port, size_t port_sz, char *path, size_t path_sz)
{
	const char *p = url;

	if (strncmp(p, "http://", 7) == 0) {
		p += 7;
		snprintf(port, port_sz, "80");
	} else if (strncmp(p, "https://", 8) == 0) {
		p += 8;
		snprintf(port, port_sz, "443");
	} else {
		return false;
	}

	const char *slash = strchr(p, '/');
	const char *colon = strchr(p, ':');

	if (colon && (!slash || colon < slash)) {
		size_t hlen = (size_t)(colon - p);
		if (hlen >= host_sz)
			hlen = host_sz - 1;
		memcpy(host, p, hlen);
		host[hlen] = '\0';

		colon++;
		const char *pend = slash ? slash : colon + strlen(colon);
		size_t plen = (size_t)(pend - colon);
		if (plen >= port_sz)
			plen = port_sz - 1;
		memcpy(port, colon, plen);
		port[plen] = '\0';
	} else {
		size_t hlen = slash ? (size_t)(slash - p)
				    : strlen(p);
		if (hlen >= host_sz)
			hlen = host_sz - 1;
		memcpy(host, p, hlen);
		host[hlen] = '\0';
	}

	if (slash)
		snprintf(path, path_sz, "%s", slash);
	else
		snprintf(path, path_sz, "/");

	return true;
}

static void webhook_do_send(const char *url, const char *event_name,
			    const char *source_name)
{
	char host[256] = {0};
	char port_str[16] = {0};
	char path[512] = {0};

	if (!parse_url(url, host, sizeof(host), port_str, sizeof(port_str),
		       path, sizeof(path))) {
		blog(LOG_WARNING, "[%s] Webhook: invalid URL '%s'",
		     "Easy IRL Stream", url);
		return;
	}

#ifdef _WIN32
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

	struct addrinfo hints = {0};
	struct addrinfo *res = NULL;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(host, port_str, &hints, &res) != 0) {
		blog(LOG_WARNING, "[%s] Webhook: DNS lookup failed for '%s'",
		     "Easy IRL Stream", host);
		return;
	}

	sock_t sock = socket(res->ai_family, res->ai_socktype,
			     res->ai_protocol);
	if (sock == SOCK_INVALID) {
		freeaddrinfo(res);
		return;
	}

	if (connect(sock, res->ai_addr, (int)res->ai_addrlen) != 0) {
		freeaddrinfo(res);
		sock_close(sock);
		return;
	}
	freeaddrinfo(res);

	char body[1024];
	snprintf(body, sizeof(body),
		 "{\"event\":\"%s\",\"source\":\"%s\",\"timestamp\":%lld}",
		 event_name, source_name, (long long)time(NULL));

	char request[2048];
	snprintf(request, sizeof(request),
		 "POST %s HTTP/1.1\r\n"
		 "Host: %s\r\n"
		 "Content-Type: application/json\r\n"
		 "Content-Length: %d\r\n"
		 "Connection: close\r\n"
		 "\r\n"
		 "%s",
		 path, host, (int)strlen(body), body);

	send(sock, request, (int)strlen(request), 0);

	char buf[512];
	while (recv(sock, buf, sizeof(buf), 0) > 0) {
	}

	sock_close(sock);

	blog(LOG_INFO, "[%s] Webhook sent: %s -> %s", "Easy IRL Stream",
	     event_name, url);
}

static void *webhook_thread_func(void *arg)
{
	struct webhook_args *wa = arg;
	webhook_do_send(wa->url, wa->event_name, wa->source_name);
	bfree(wa->url);
	bfree(wa->event_name);
	bfree(wa->source_name);
	bfree(wa);
	return NULL;
}

void webhook_send_async(const char *url, const char *event_name,
			const char *source_name)
{
	if (!url || !url[0])
		return;

	struct webhook_args *wa = bzalloc(sizeof(*wa));
	wa->url = bstrdup(url);
	wa->event_name = bstrdup(event_name);
	wa->source_name = bstrdup(source_name);

	pthread_t thread;
	if (pthread_create(&thread, NULL, webhook_thread_func, wa) == 0) {
		pthread_detach(thread);
	} else {
		bfree(wa->url);
		bfree(wa->event_name);
		bfree(wa->source_name);
		bfree(wa);
	}
}

static void *cmd_thread_func(void *arg)
{
	struct cmd_args *ca = arg;
	blog(LOG_INFO, "[%s] Executing command: %s", "Easy IRL Stream",
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
