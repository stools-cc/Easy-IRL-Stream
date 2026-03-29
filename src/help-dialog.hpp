#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void help_dialog_show(const char *local_ip, const char *external_ip,
		      const char *version, const char *locale);

void update_dialog_show(const char *new_version, const char *locale);

#ifdef __cplusplus
}
#endif
