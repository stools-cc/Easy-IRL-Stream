#include "obfuscation.h"

static constexpr char K = 0x5A;

template<unsigned N>
struct XorStr {
	char data[N];
	constexpr XorStr(const char (&s)[N]) : data{}
	{
		for (unsigned i = 0; i < N; i++)
			data[i] = s[i] ^ K;
	}
};

template<unsigned N>
static void xor_dec(char *out, const XorStr<N> &x)
{
	for (unsigned i = 0; i < N; i++)
		out[i] = x.data[i] ^ K;
}

#define OBF_FUNC(fn, literal)                       \
	static constexpr XorStr _enc_##fn(literal); \
	extern "C" const char *fn(void)             \
	{                                           \
		static char buf[sizeof(literal)];   \
		static int ready;                   \
		if (!ready) {                       \
			xor_dec(buf, _enc_##fn);    \
			ready = 1;                  \
		}                                   \
		return buf;                         \
	}

OBF_FUNC(obf_stools_host,         "stools.cc")
OBF_FUNC(obf_api_settings_path,   "/api/plugin/settings")
OBF_FUNC(obf_api_obs_info_path,   "/api/plugin/obs-info")
OBF_FUNC(obf_api_version_path,    "/api/plugin/version")
OBF_FUNC(obf_dash_tools_path,     "/dashboard/tools")
OBF_FUNC(obf_dash_downloads_path, "/dashboard/downloads")
OBF_FUNC(obf_ipify_host,          "api.ipify.org")
OBF_FUNC(obf_duckdns_host,        "www.duckdns.org")
OBF_FUNC(obf_ua_prefix,           "easy-irl-stream/")
OBF_FUNC(obf_https_prefix,        "https://")
OBF_FUNC(obf_duckdns_update_fmt,  "/update?domains=%s&token=%s&verbose=true")
OBF_FUNC(obf_auth_bearer_fmt,     "Authorization: Bearer %s")
