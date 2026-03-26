#pragma once

#include <obs-module.h>
#include <util/threading.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define SRTLA_SOCKET SOCKET
#define SRTLA_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#define SRTLA_SOCKET int
#define SRTLA_INVALID_SOCKET (-1)
#define closesocket close
#endif

#define SRTLA_MAX_GROUPS 8
#define SRTLA_MAX_CONNS  8
#define SRTLA_MAX_PKT    1500
#define SRTLA_GROUP_ID_LEN 256

/* SRTLA control types (without 0x8000 bit, that's added separately) */
#define SRTLA_TYPE_KEEPALIVE 0x1000
#define SRTLA_TYPE_ACK       0x1100
#define SRTLA_TYPE_REG1      0x1200
#define SRTLA_TYPE_REG2      0x1201
#define SRTLA_TYPE_REG3      0x1202
#define SRTLA_TYPE_REG_ERR   0x1210
#define SRTLA_TYPE_REG_NGP   0x1211
#define SRTLA_TYPE_REG_NAK   0x1212

/* SRT control type bit (bit 15) */
#define SRT_CONTROL_BIT      0x8000

/* SRTLA ACK: 2 bytes header + 2 bytes padding + up to 10 sequence numbers */
#define SRTLA_ACK_MAX_SNS    10
#define SRTLA_ACK_PKT_LEN    (4 + SRTLA_ACK_MAX_SNS * 4)

struct srtla_conn {
	struct sockaddr_storage addr;
	socklen_t addr_len;
	uint64_t last_activity_ns;
	uint64_t total_bytes;
	bool active;
};

struct srtla_group {
	uint8_t group_id[SRTLA_GROUP_ID_LEN];
	struct srtla_conn conns[SRTLA_MAX_CONNS];
	int num_conns;
	SRTLA_SOCKET srt_sock;
	uint64_t last_activity_ns;
	bool active;
	/* SRTLA ACK tracking */
	uint32_t ack_sns[SRTLA_ACK_MAX_SNS];
	int ack_sn_count;
};

struct srtla_state {
	SRTLA_SOCKET listen_sock;
	struct srtla_group groups[SRTLA_MAX_GROUPS];
	int srt_port;
	int listen_port;
	volatile bool running;
	pthread_t thread;
	bool thread_created;
};

void srtla_server_start(struct srtla_state *state, int listen_port,
			int srt_port);
void srtla_server_stop(struct srtla_state *state);
