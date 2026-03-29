#pragma once

#include <stddef.h>

/* Simple XOR encode/decode — symmetric operation */
static inline void xor_crypt(char *buf, const char *src, size_t len)
{
	for (size_t i = 0; i < len; i++)
		buf[i] = src[i] ^ 0x5A;
	buf[len] = '\0';
}

/* Obfuscated string accessors (implemented in obfuscation.cpp) */
#ifdef __cplusplus
extern "C" {
#endif

const char *obf_stools_host(void);
const char *obf_api_settings_path(void);
const char *obf_api_obs_info_path(void);
const char *obf_api_version_path(void);
const char *obf_dash_tools_path(void);
const char *obf_dash_downloads_path(void);
const char *obf_ipify_host(void);
const char *obf_duckdns_host(void);
const char *obf_ua_prefix(void);
const char *obf_https_prefix(void);
const char *obf_duckdns_update_fmt(void);
const char *obf_auth_bearer_fmt(void);

#ifdef __cplusplus
}
#endif
