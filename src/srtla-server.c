#include "srtla-server.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define PLUGIN_NAME "Easy IRL Stream"

#define SRTLA_REG_PKT_SIZE (2 + SRTLA_GROUP_ID_LEN)

static uint16_t srtla_get_type(const uint8_t *buf, int len)
{
	if (len < 4)
		return 0;
	uint16_t v = ((uint16_t)buf[0] << 8) | buf[1];
	if (!(v & SRT_CONTROL_BIT))
		return 0;
	uint16_t type = v & 0x7FFF;
	return (type >= 0x1000) ? type : 0;
}

static bool is_srt_data_packet(const uint8_t *buf, int len)
{
	if (len < 4)
		return false;
	return (buf[0] & 0x80) == 0;
}

static uint32_t get_srt_sequence_number(const uint8_t *buf)
{
	return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
	       ((uint32_t)buf[2] << 8) | buf[3];
}

static void srtla_build_header(uint8_t *buf, uint16_t type)
{
	uint16_t v = SRT_CONTROL_BIT | type;
	buf[0] = (uint8_t)(v >> 8);
	buf[1] = (uint8_t)(v & 0xFF);
}

static bool sockaddr_equal(const struct sockaddr_storage *a, socklen_t alen,
			   const struct sockaddr_storage *b, socklen_t blen)
{
	(void)alen;
	(void)blen;
	const struct sockaddr_in *sa = (const struct sockaddr_in *)a;
	const struct sockaddr_in *sb = (const struct sockaddr_in *)b;
	if (sa->sin_family != sb->sin_family)
		return false;
	if (sa->sin_family == AF_INET)
		return sa->sin_port == sb->sin_port &&
		       sa->sin_addr.s_addr == sb->sin_addr.s_addr;
	return false;
}

static bool sockaddr_same_ip(const struct sockaddr_storage *a,
			     const struct sockaddr_storage *b)
{
	const struct sockaddr_in *sa = (const struct sockaddr_in *)a;
	const struct sockaddr_in *sb = (const struct sockaddr_in *)b;
	if (sa->sin_family != sb->sin_family)
		return false;
	if (sa->sin_family == AF_INET)
		return sa->sin_addr.s_addr == sb->sin_addr.s_addr;
	return false;
}

static struct srtla_group *find_group(struct srtla_state *state,
				      const uint8_t *group_id)
{
	for (int i = 0; i < SRTLA_MAX_GROUPS; i++) {
		if (state->groups[i].active &&
		    memcmp(state->groups[i].group_id, group_id,
			   SRTLA_GROUP_ID_LEN) == 0)
			return &state->groups[i];
	}
	return NULL;
}

static struct srtla_group *alloc_group(struct srtla_state *state)
{
	for (int i = 0; i < SRTLA_MAX_GROUPS; i++) {
		if (!state->groups[i].active)
			return &state->groups[i];
	}
	return NULL;
}

static SRTLA_SOCKET create_srt_forward_sock(int srt_port)
{
	SRTLA_SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == SRTLA_INVALID_SOCKET)
		return SRTLA_INVALID_SOCKET;

	struct sockaddr_in dst = {0};
	dst.sin_family = AF_INET;
	dst.sin_port = htons((uint16_t)srt_port);
	dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (connect(s, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
		closesocket(s);
		return SRTLA_INVALID_SOCKET;
	}

#ifdef _WIN32
	u_long nonblock = 1;
	ioctlsocket(s, FIONBIO, &nonblock);
#else
	{
		int flags = fcntl(s, F_GETFL, 0);
		fcntl(s, F_SETFL, flags | O_NONBLOCK);
	}
#endif

	return s;
}

static void group_cleanup(struct srtla_group *g)
{
	if (g->srt_sock != SRTLA_INVALID_SOCKET) {
		closesocket(g->srt_sock);
		g->srt_sock = SRTLA_INVALID_SOCKET;
	}
	memset(g, 0, sizeof(*g));
	g->srt_sock = SRTLA_INVALID_SOCKET;
}

static void add_connection_to_group(struct srtla_group *g,
				    const struct sockaddr_storage *from,
				    socklen_t from_len)
{
	for (int i = 0; i < g->num_conns; i++) {
		if (sockaddr_equal(&g->conns[i].addr, g->conns[i].addr_len,
				   from, from_len)) {
			g->conns[i].last_activity_ns = os_gettime_ns();
			return;
		}
	}
	if (g->num_conns < SRTLA_MAX_CONNS) {
		struct srtla_conn *c = &g->conns[g->num_conns++];
		memcpy(&c->addr, from, from_len);
		c->addr_len = from_len;
		c->last_activity_ns = os_gettime_ns();
		c->active = true;
		blog(LOG_INFO,
		     "[%s] SRTLA: connection %d added to group",
		     PLUGIN_NAME, g->num_conns);
	}
}

static void send_srtla_ack(struct srtla_state *state, struct srtla_group *g)
{
	if (g->ack_sn_count == 0)
		return;

	int pkt_len = 4 + g->ack_sn_count * 4;
	uint8_t pkt[SRTLA_ACK_PKT_LEN];
	memset(pkt, 0, sizeof(pkt));
	srtla_build_header(pkt, SRTLA_TYPE_ACK);
	pkt[2] = 0;
	pkt[3] = 0;

	for (int i = 0; i < g->ack_sn_count; i++) {
		int off = 4 + i * 4;
		pkt[off + 0] = (uint8_t)(g->ack_sns[i] >> 24);
		pkt[off + 1] = (uint8_t)(g->ack_sns[i] >> 16);
		pkt[off + 2] = (uint8_t)(g->ack_sns[i] >> 8);
		pkt[off + 3] = (uint8_t)(g->ack_sns[i]);
	}

	for (int j = 0; j < g->num_conns; j++) {
		sendto(state->listen_sock, (const char *)pkt, pkt_len, 0,
		       (const struct sockaddr *)&g->conns[j].addr,
		       g->conns[j].addr_len);
	}

	g->ack_sn_count = 0;
}

/*
 * REG1: Client wants to create a new SRTLA group.
 * Packet: 2 bytes type + 256 bytes client random.
 * Response: REG2 with first 128 bytes from client + 128 bytes server random.
 */
static void handle_reg1(struct srtla_state *state, const uint8_t *buf, int len,
			const struct sockaddr_storage *from, socklen_t from_len)
{
	if (len < SRTLA_REG_PKT_SIZE) {
		blog(LOG_WARNING,
		     "[%s] SRTLA: REG1 too short (%d, need %d)",
		     PLUGIN_NAME, len, SRTLA_REG_PKT_SIZE);
		return;
	}

	blog(LOG_INFO, "[%s] SRTLA: Got REG1 (create group)", PLUGIN_NAME);

	uint8_t group_id[SRTLA_GROUP_ID_LEN];
	memcpy(group_id, buf + 2, 128);

	for (int i = 0; i < 128; i++)
		group_id[128 + i] = (uint8_t)(rand() & 0xFF);

	if (find_group(state, group_id)) {
		blog(LOG_WARNING,
		     "[%s] SRTLA: group ID collision, ignoring",
		     PLUGIN_NAME);
		return;
	}

	struct srtla_group *g = alloc_group(state);
	if (!g) {
		blog(LOG_WARNING,
		     "[%s] SRTLA: max groups reached",
		     PLUGIN_NAME);
		return;
	}

	memset(g, 0, sizeof(*g));
	g->srt_sock = SRTLA_INVALID_SOCKET;
	memcpy(g->group_id, group_id, SRTLA_GROUP_ID_LEN);
	g->srt_sock = create_srt_forward_sock(state->srt_port);
	if (g->srt_sock == SRTLA_INVALID_SOCKET) {
		blog(LOG_WARNING,
		     "[%s] SRTLA: failed to create SRT forward socket",
		     PLUGIN_NAME);
		return;
	}
	g->active = true;
	g->last_activity_ns = os_gettime_ns();

	blog(LOG_INFO,
	     "[%s] SRTLA: group created, sending REG2 response",
	     PLUGIN_NAME);

	uint8_t resp[SRTLA_REG_PKT_SIZE];
	srtla_build_header(resp, SRTLA_TYPE_REG2);
	memcpy(resp + 2, group_id, SRTLA_GROUP_ID_LEN);
	sendto(state->listen_sock, (const char *)resp, SRTLA_REG_PKT_SIZE, 0,
	       (const struct sockaddr *)from, from_len);
}

/*
 * REG2: Client wants to register a connection to an existing group.
 * Packet: 2 bytes type + 256 bytes group ID.
 * Response: REG3 if group found, REG_NGP if not.
 */
static void handle_reg2(struct srtla_state *state, const uint8_t *buf, int len,
			const struct sockaddr_storage *from, socklen_t from_len)
{
	if (len < SRTLA_REG_PKT_SIZE) {
		blog(LOG_WARNING,
		     "[%s] SRTLA: REG2 too short (%d, need %d)",
		     PLUGIN_NAME, len, SRTLA_REG_PKT_SIZE);
		return;
	}

	blog(LOG_INFO,
	     "[%s] SRTLA: Got REG2 (register connection)",
	     PLUGIN_NAME);

	const uint8_t *gid = buf + 2;
	struct srtla_group *g = find_group(state, gid);

	if (!g) {
		blog(LOG_INFO,
		     "[%s] SRTLA: unknown group, sending REG_NGP",
		     PLUGIN_NAME);
		uint8_t ngp[2];
		srtla_build_header(ngp, SRTLA_TYPE_REG_NGP);
		sendto(state->listen_sock, (const char *)ngp, 2, 0,
		       (const struct sockaddr *)from, from_len);
		return;
	}

	add_connection_to_group(g, from, from_len);
	g->last_activity_ns = os_gettime_ns();

	blog(LOG_INFO,
	     "[%s] SRTLA: connection registered, sending REG3 (%d conns)",
	     PLUGIN_NAME, g->num_conns);

	uint8_t reg3[2];
	srtla_build_header(reg3, SRTLA_TYPE_REG3);
	sendto(state->listen_sock, (const char *)reg3, 2, 0,
	       (const struct sockaddr *)from, from_len);
}

static void handle_keepalive(struct srtla_state *state, const uint8_t *buf,
			     int len,
			     const struct sockaddr_storage *from,
			     socklen_t from_len)
{
	int echo_len = (len > SRTLA_MAX_PKT) ? SRTLA_MAX_PKT : len;
	sendto(state->listen_sock, (const char *)buf, echo_len, 0,
	       (const struct sockaddr *)from, from_len);

	for (int i = 0; i < SRTLA_MAX_GROUPS; i++) {
		if (!state->groups[i].active)
			continue;
		for (int j = 0; j < state->groups[i].num_conns; j++) {
			if (sockaddr_equal(&state->groups[i].conns[j].addr,
					   state->groups[i].conns[j].addr_len,
					   from, from_len)) {
				state->groups[i].conns[j].last_activity_ns =
					os_gettime_ns();
				state->groups[i].last_activity_ns =
					os_gettime_ns();
				return;
			}
		}
	}
}

static struct srtla_group *find_group_by_addr(struct srtla_state *state,
					      const struct sockaddr_storage *from,
					      socklen_t from_len)
{
	for (int i = 0; i < SRTLA_MAX_GROUPS; i++) {
		if (!state->groups[i].active)
			continue;

		for (int j = 0; j < state->groups[i].num_conns; j++) {
			if (sockaddr_equal(&state->groups[i].conns[j].addr,
					   state->groups[i].conns[j].addr_len,
					   from, from_len)) {
				state->groups[i].conns[j].last_activity_ns =
					os_gettime_ns();
				state->groups[i].last_activity_ns =
					os_gettime_ns();
				return &state->groups[i];
			}
		}

		if (state->groups[i].num_conns > 0 &&
		    sockaddr_same_ip(&state->groups[i].conns[0].addr, from)) {
			add_connection_to_group(&state->groups[i], from,
						from_len);
			state->groups[i].last_activity_ns = os_gettime_ns();
			return &state->groups[i];
		}
	}
	return NULL;
}

static void cleanup_stale_groups(struct srtla_state *state)
{
	uint64_t now = os_gettime_ns();
	for (int i = 0; i < SRTLA_MAX_GROUPS; i++) {
		if (!state->groups[i].active)
			continue;
		uint64_t age_ms =
			(now - state->groups[i].last_activity_ns) / 1000000;
		if (age_ms > 30000) {
			blog(LOG_INFO,
			     "[%s] SRTLA: group %d timed out (age=%llu ms, last_ns=%llu, now_ns=%llu)",
			     PLUGIN_NAME, i,
			     (unsigned long long)age_ms,
			     (unsigned long long)state->groups[i].last_activity_ns,
			     (unsigned long long)now);
			group_cleanup(&state->groups[i]);
		}
	}
}

static void *srtla_thread_func(void *arg)
{
	struct srtla_state *state = arg;

	os_set_thread_name("easy-irl-srtla");

	state->listen_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (state->listen_sock == SRTLA_INVALID_SOCKET) {
		blog(LOG_ERROR, "[%s] SRTLA: failed to create socket",
		     PLUGIN_NAME);
		return NULL;
	}

	int reuse = 1;
	setsockopt(state->listen_sock, SOL_SOCKET, SO_REUSEADDR,
		   (const char *)&reuse, sizeof(reuse));

	struct sockaddr_in bind_addr = {0};
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_port = htons((uint16_t)state->listen_port);
	bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(state->listen_sock, (struct sockaddr *)&bind_addr,
		 sizeof(bind_addr)) != 0) {
		blog(LOG_ERROR,
		     "[%s] SRTLA: failed to bind port %d",
		     PLUGIN_NAME, state->listen_port);
		closesocket(state->listen_sock);
		state->listen_sock = SRTLA_INVALID_SOCKET;
		return NULL;
	}

	/* Set listen socket to non-blocking for draining all packets */
#ifdef _WIN32
	u_long nonblock = 1;
	ioctlsocket(state->listen_sock, FIONBIO, &nonblock);
#else
	{
		int flags = fcntl(state->listen_sock, F_GETFL, 0);
		fcntl(state->listen_sock, F_SETFL, flags | O_NONBLOCK);
	}
#endif

	blog(LOG_INFO,
	     "[%s] SRTLA: listening on UDP port %d, forwarding to SRT port %d",
	     PLUGIN_NAME, state->listen_port, state->srt_port);

	uint64_t last_cleanup = os_gettime_ns();

	while (state->running) {
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(state->listen_sock, &readfds);

		SRTLA_SOCKET maxfd = state->listen_sock;

		for (int i = 0; i < SRTLA_MAX_GROUPS; i++) {
			if (state->groups[i].active &&
			    state->groups[i].srt_sock != SRTLA_INVALID_SOCKET) {
				FD_SET(state->groups[i].srt_sock, &readfds);
				if (state->groups[i].srt_sock > maxfd)
					maxfd = state->groups[i].srt_sock;
			}
		}

		struct timeval tv = {0, 50000};
		int ret = select((int)(maxfd + 1), &readfds, NULL, NULL, &tv);
		if (ret <= 0)
			goto cleanup_check;

		if (FD_ISSET(state->listen_sock, &readfds)) {
		    for (int pkt_iter = 0; pkt_iter < 256; pkt_iter++) {
			uint8_t buf[SRTLA_MAX_PKT];
			struct sockaddr_storage from;
			socklen_t from_len = sizeof(from);

			int n = recvfrom(state->listen_sock, (char *)buf,
					 sizeof(buf), 0,
					 (struct sockaddr *)&from, &from_len);
			if (n <= 0)
				break;

			uint16_t type = srtla_get_type(buf, n);

			switch (type) {
				case SRTLA_TYPE_REG1:
					handle_reg1(state, buf, n, &from,
						    from_len);
					break;
				case SRTLA_TYPE_REG2:
					handle_reg2(state, buf, n, &from,
						    from_len);
					break;
				case SRTLA_TYPE_KEEPALIVE:
					handle_keepalive(state, buf, n, &from,
							 from_len);
					break;
			default: {
				struct srtla_group *g =
					find_group_by_addr(state, &from,
							   from_len);

				if (!g) {
					g = alloc_group(state);
					if (g) {
						memset(g, 0, sizeof(*g));
						g->srt_sock =
							SRTLA_INVALID_SOCKET;
						memset(g->group_id, 0xFF,
						       SRTLA_GROUP_ID_LEN);
						g->srt_sock =
							create_srt_forward_sock(
								state->srt_port);
						if (g->srt_sock !=
						    SRTLA_INVALID_SOCKET) {
							g->active = true;
							add_connection_to_group(
								g, &from,
								from_len);
							g->last_activity_ns =
								os_gettime_ns();
							blog(LOG_INFO,
							     "[%s] SRTLA: auto-registered client (%d bytes)",
							     PLUGIN_NAME, n);
						} else {
							memset(g, 0,
							       sizeof(*g));
							g->srt_sock =
								SRTLA_INVALID_SOCKET;
							g = NULL;
						}
					}
				}

				if (g && g->srt_sock !=
						 SRTLA_INVALID_SOCKET) {
					int sent = send(g->srt_sock,
							(const char *)buf,
							n, 0);
					g->last_activity_ns = os_gettime_ns();
					if (sent < 0) {
						blog(LOG_WARNING,
						     "[%s] SRTLA: forward failed (err=%d)",
						     PLUGIN_NAME,
#ifdef _WIN32
						     WSAGetLastError()
#else
						     errno
#endif
						);
					}

					if (is_srt_data_packet(buf, n) &&
					    n >= 4) {
						uint32_t sn =
							get_srt_sequence_number(
								buf);
						g->ack_sns
							[g->ack_sn_count++] =
							sn;
						if (g->ack_sn_count >=
						    SRTLA_ACK_MAX_SNS) {
							send_srtla_ack(
								state, g);
						}
					}
				}
				break;
			}
				}
			}
		}

		/* Responses from SRT server back to clients */
		for (int i = 0; i < SRTLA_MAX_GROUPS; i++) {
			struct srtla_group *g = &state->groups[i];
			if (!g->active ||
			    g->srt_sock == SRTLA_INVALID_SOCKET ||
			    !FD_ISSET(g->srt_sock, &readfds))
				continue;

		    for (int resp_iter = 0; resp_iter < 256; resp_iter++) {
			uint8_t buf[SRTLA_MAX_PKT];
			int n = recv(g->srt_sock, (char *)buf, sizeof(buf), 0);
			if (n <= 0)
				break;

			g->last_activity_ns = os_gettime_ns();

			if (g->num_conns > 0) {
				bool is_data = is_srt_data_packet(buf, n);
				uint16_t srt_type = 0;
				if (!is_data && n >= 4) {
					srt_type =
						(((uint16_t)buf[0] << 8) |
						 buf[1]) &
						0x7FFF;
				}

				bool is_ack = (!is_data && srt_type == 0x0002);
				bool is_nak = (!is_data && srt_type == 0x0003);

				if (is_ack || is_nak) {
					for (int j = 0; j < g->num_conns;
					     j++) {
						sendto(state->listen_sock,
						       (const char *)buf, n, 0,
						       (const struct sockaddr
								*)&g->conns[j]
							       .addr,
						       g->conns[j].addr_len);
					}
				} else {
					struct srtla_conn *c = &g->conns[0];
					for (int j = 1; j < g->num_conns;
					     j++) {
						if (g->conns[j]
							    .last_activity_ns >
						    c->last_activity_ns)
							c = &g->conns[j];
					}
				sendto(state->listen_sock,
				       (const char *)buf, n, 0,
				       (const struct sockaddr *)&c->addr,
				       c->addr_len);
			}
		    }
		}
	}

	cleanup_check:;
		uint64_t now = os_gettime_ns();
		if ((now - last_cleanup) / 1000000 > 5000) {
			cleanup_stale_groups(state);
			last_cleanup = now;
		}
	}

	for (int i = 0; i < SRTLA_MAX_GROUPS; i++) {
		if (state->groups[i].active)
			group_cleanup(&state->groups[i]);
	}
	closesocket(state->listen_sock);
	state->listen_sock = SRTLA_INVALID_SOCKET;

	blog(LOG_INFO, "[%s] SRTLA: server stopped", PLUGIN_NAME);
	return NULL;
}

void srtla_server_start(struct srtla_state *state, int listen_port,
			int srt_port)
{
	if (state->thread_created)
		srtla_server_stop(state);

	memset(state, 0, sizeof(*state));
	state->listen_sock = SRTLA_INVALID_SOCKET;
	for (int i = 0; i < SRTLA_MAX_GROUPS; i++)
		state->groups[i].srt_sock = SRTLA_INVALID_SOCKET;

	state->listen_port = listen_port;
	state->srt_port = srt_port;
	state->running = true;

	if (pthread_create(&state->thread, NULL, srtla_thread_func, state) ==
	    0) {
		state->thread_created = true;
	} else {
		blog(LOG_ERROR, "[%s] SRTLA: failed to create thread",
		     PLUGIN_NAME);
		state->running = false;
	}
}

void srtla_server_stop(struct srtla_state *state)
{
	if (!state->thread_created)
		return;

	state->running = false;
	pthread_join(state->thread, NULL);
	state->thread_created = false;
}
