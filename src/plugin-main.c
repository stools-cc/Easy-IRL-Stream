#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/threading.h>
#include <string.h>
#include <stdio.h>
#include <curl/curl.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <net/if.h>
#define closesocket close
#define SOCKET      int
#define INVALID_SOCKET (-1)
#endif

#include "irl-source.h"
#include "obfuscation.h"
#include "translations.h"
#include "help-dialog.hpp"
#include "stats-dialog.hpp"

OBS_DECLARE_MODULE()

/* ---- IP detection (global) ---- */

char g_local_ip[64] = "";
char g_external_ip[64] = "";

static void detect_local_ip(void)
{
#ifdef _WIN32
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);

	ULONG buf_size = 15000;
	PIP_ADAPTER_ADDRESSES addrs = (PIP_ADAPTER_ADDRESSES)malloc(buf_size);
	if (!addrs) {
		snprintf(g_local_ip, sizeof(g_local_ip), "?.?.?.?");
		return;
	}

	ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS;
	ULONG ret = GetAdaptersAddresses(AF_INET, flags, NULL, addrs,
					 &buf_size);
	if (ret == ERROR_BUFFER_OVERFLOW) {
		free(addrs);
		addrs = (PIP_ADAPTER_ADDRESSES)malloc(buf_size);
		if (!addrs) {
			snprintf(g_local_ip, sizeof(g_local_ip), "?.?.?.?");
			return;
		}
		ret = GetAdaptersAddresses(AF_INET, flags, NULL, addrs,
					  &buf_size);
	}
	if (ret != NO_ERROR) {
		free(addrs);
		snprintf(g_local_ip, sizeof(g_local_ip), "?.?.?.?");
		return;
	}

	for (PIP_ADAPTER_ADDRESSES a = addrs; a; a = a->Next) {
		if (a->OperStatus != IfOperStatusUp)
			continue;
		if (!a->FirstGatewayAddress)
			continue;
		if (a->IfType != IF_TYPE_ETHERNET_CSMACD &&
		    a->IfType != IF_TYPE_IEEE80211)
			continue;

		PIP_ADAPTER_UNICAST_ADDRESS ua = a->FirstUnicastAddress;
		for (; ua; ua = ua->Next) {
			struct sockaddr_in *sa =
				(struct sockaddr_in *)ua->Address.lpSockaddr;
			if (sa->sin_family == AF_INET) {
				inet_ntop(AF_INET, &sa->sin_addr, g_local_ip,
					  sizeof(g_local_ip));
				free(addrs);
				return;
			}
		}
	}
	free(addrs);
	snprintf(g_local_ip, sizeof(g_local_ip), "?.?.?.?");

#else
	struct ifaddrs *ifas, *ifa;
	if (getifaddrs(&ifas) == -1) {
		snprintf(g_local_ip, sizeof(g_local_ip), "?.?.?.?");
		return;
	}
	for (ifa = ifas; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
			continue;
		if (ifa->ifa_flags & IFF_LOOPBACK)
			continue;
		if (!(ifa->ifa_flags & IFF_UP))
			continue;
		struct sockaddr_in *sa =
			(struct sockaddr_in *)ifa->ifa_addr;
		inet_ntop(AF_INET, &sa->sin_addr, g_local_ip,
			  sizeof(g_local_ip));
		break;
	}
	freeifaddrs(ifas);
	if (!g_local_ip[0])
		snprintf(g_local_ip, sizeof(g_local_ip), "?.?.?.?");
#endif
}

static void http_get_body(const char *host, const char *path, char *out,
			  size_t out_sz)
{
	struct addrinfo hints = {0}, *res = NULL;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(host, "80", &hints, &res) != 0) {
		snprintf(out, out_sz, "?");
		return;
	}

	SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (s == INVALID_SOCKET) {
		freeaddrinfo(res);
		snprintf(out, out_sz, "?");
		return;
	}

	if (connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) {
		freeaddrinfo(res);
		closesocket(s);
		snprintf(out, out_sz, "?");
		return;
	}
	freeaddrinfo(res);

	char req[512];
	snprintf(req, sizeof(req),
		 "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
		 path, host);
	send(s, req, (int)strlen(req), 0);

	char response[2048] = {0};
	int total = 0, n;
	while ((n = recv(s, response + total,
			 (int)(sizeof(response) - total - 1), 0)) > 0)
		total += n;
	response[total] = '\0';
	closesocket(s);

	char *body = strstr(response, "\r\n\r\n");
	if (body) {
		body += 4;
		while (*body == ' ' || *body == '\r' || *body == '\n')
			body++;
		char *end = body;
		while (*end && *end != '\r' && *end != '\n' && *end != ' ')
			end++;
		*end = '\0';
		snprintf(out, out_sz, "%s", body);
	} else {
		snprintf(out, out_sz, "?");
	}
}

/* ---- DuckDNS update ---- */

void duckdns_update(const char *domain, const char *token)
{
	if (!domain || !domain[0] || !token || !token[0])
		return;

	char path[512];
	snprintf(path, sizeof(path), obf_duckdns_update_fmt(), domain, token);

	char result[256] = {0};
	http_get_body(obf_duckdns_host(), path, result, sizeof(result));

	if (strncmp(result, "OK", 2) == 0 || strncmp(result, "KO", 2) == 0) {
		dbg_log(LOG_DEBUG, "[%s] DuckDNS update for %s.duckdns.org: %s",
		     PLUGIN_NAME, domain, result);
	} else {
		dbg_log(LOG_WARNING, "[%s] DuckDNS update failed: %s",
		     PLUGIN_NAME, result);
	}
}

/* ---- IP monitoring ---- */

static volatile bool g_ip_thread_active = false;
static pthread_t g_ip_thread;

static void trigger_duckdns_update(void)
{
	for (int i = 0; i < g_irl_source_count && i < MAX_IRL_SOURCES; i++) {
		struct irl_source_data *d = g_irl_sources[i];
		if (!d) continue;

		pthread_mutex_lock(&d->mutex);
		bool has_dns = d->duckdns_domain && d->duckdns_domain[0] &&
			       d->duckdns_token && d->duckdns_token[0];
		char *dd = has_dns ? bstrdup(d->duckdns_domain) : NULL;
		char *dt = has_dns ? bstrdup(d->duckdns_token) : NULL;
		pthread_mutex_unlock(&d->mutex);

		if (dd && dt) {
			duckdns_update(dd, dt);
			bfree(dd);
			bfree(dt);
			break;
		}
		bfree(dd);
		bfree(dt);
	}
}

#define IP_CHECK_INTERVAL_SEC 60

static void *ip_detect_thread(void *arg)
{
	UNUSED_PARAMETER(arg);

	detect_local_ip();
	http_get_body(obf_ipify_host(), "/", g_external_ip,
		      sizeof(g_external_ip));
	dbg_log(LOG_DEBUG, "[%s] Local IP: %s, External IP: %s", PLUGIN_NAME,
	     g_local_ip, g_external_ip);

	while (g_ip_thread_active) {
		for (int i = 0; i < IP_CHECK_INTERVAL_SEC * 10 &&
				g_ip_thread_active; i++)
			os_sleep_ms(100);

		if (!g_ip_thread_active)
			break;

		char new_ip[64] = {0};
		http_get_body(obf_ipify_host(), "/", new_ip, sizeof(new_ip));

		if (new_ip[0] && strcmp(new_ip, "?") != 0 &&
		    strcmp(new_ip, g_external_ip) != 0) {
			dbg_log(LOG_DEBUG,
			     "[%s] External IP changed: %s -> %s",
			     PLUGIN_NAME, g_external_ip, new_ip);
			snprintf(g_external_ip, sizeof(g_external_ip),
				 "%s", new_ip);
			trigger_duckdns_update();
		}
	}

	return NULL;
}

/* ---- Tools menu ---- */

static void tools_menu_cb(void *private_data)
{
	UNUSED_PARAMETER(private_data);
	help_dialog_show(g_local_ip, g_external_ip, PLUGIN_VERSION,
			 obs_get_locale());
}

static void tools_stats_cb(void *private_data)
{
	UNUSED_PARAMETER(private_data);
	stats_dialog_show(obs_get_locale());
}

/* ---- module lifecycle ---- */

bool obs_module_load(void)
{
	curl_global_init(CURL_GLOBAL_DEFAULT);

	curl_version_info_data *vi = curl_version_info(CURLVERSION_NOW);
	if (vi) {
		dbg_log(LOG_INFO,
		     "[%s] curl %s, SSL: %s, features: 0x%x",
		     PLUGIN_NAME, vi->version,
		     vi->ssl_version ? vi->ssl_version : "none",
		     (unsigned)vi->features);
	}

	obs_register_source(&irl_source_info);

	g_ip_thread_active = true;
	if (pthread_create(&g_ip_thread, NULL, ip_detect_thread, NULL) != 0)
		g_ip_thread_active = false;

	dbg_log(LOG_INFO, "[%s] Plugin loaded (v%s)", PLUGIN_NAME, PLUGIN_VERSION);
	return true;
}

void obs_module_post_load(void)
{
	obs_frontend_add_tools_menu_item(tr_tools_menu_help(),
					 tools_menu_cb, NULL);
	obs_frontend_add_tools_menu_item(tr_tools_menu_stats(),
					 tools_stats_cb, NULL);
	dbg_log(LOG_DEBUG, "[%s] Tools menu registered", PLUGIN_NAME);
}

void obs_module_unload(void)
{
	if (g_ip_thread_active) {
		g_ip_thread_active = false;
		pthread_join(g_ip_thread, NULL);
	}

	curl_global_cleanup();
	dbg_log(LOG_INFO, "[%s] Plugin unloaded", PLUGIN_NAME);
}
