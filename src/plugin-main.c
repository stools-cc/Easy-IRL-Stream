#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/threading.h>
#include <string.h>
#include <stdio.h>

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

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("easy-irl-stream", "en-US")

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

static void *ip_detect_thread(void *arg)
{
	UNUSED_PARAMETER(arg);
	detect_local_ip();
	http_get_body("api.ipify.org", "/", g_external_ip,
		      sizeof(g_external_ip));
	blog(LOG_INFO, "[%s] Local IP: %s, External IP: %s", PLUGIN_NAME,
	     g_local_ip, g_external_ip);
	return NULL;
}

/* ---- DuckDNS update ---- */

void duckdns_update(const char *domain, const char *token)
{
	if (!domain || !domain[0] || !token || !token[0])
		return;

	char path[512];
	snprintf(path, sizeof(path),
		 "/update?domains=%s&token=%s&verbose=true", domain, token);

	char result[256] = {0};
	http_get_body("www.duckdns.org", path, result, sizeof(result));

	if (strncmp(result, "OK", 2) == 0 || strncmp(result, "KO", 2) == 0) {
		blog(LOG_INFO, "[%s] DuckDNS update for %s.duckdns.org: %s",
		     PLUGIN_NAME, domain, result);
	} else {
		blog(LOG_WARNING, "[%s] DuckDNS update failed: %s",
		     PLUGIN_NAME, result);
	}
}

/* ---- Tools menu: Help / FAQ dialog (Qt) ---- */

#include "help-dialog.hpp"
#include "stats-dialog.hpp"

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
	obs_register_source(&irl_source_info);

	pthread_t thread;
	if (pthread_create(&thread, NULL, ip_detect_thread, NULL) == 0)
		pthread_detach(thread);

	blog(LOG_INFO, "[%s] Plugin loaded (v%s)", PLUGIN_NAME, PLUGIN_VERSION);
	return true;
}

void obs_module_post_load(void)
{
	obs_frontend_add_tools_menu_item(obs_module_text("ToolsMenuEntry"),
					 tools_menu_cb, NULL);
	obs_frontend_add_tools_menu_item(obs_module_text("ToolsMenuStats"),
					 tools_stats_cb, NULL);
	blog(LOG_INFO, "[%s] Tools menu entry registered", PLUGIN_NAME);
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[%s] Plugin unloaded", PLUGIN_NAME);
}
