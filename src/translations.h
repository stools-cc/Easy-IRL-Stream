#pragma once

#include <obs.h>

static inline int tr_is_de(void)
{
	const char *loc = obs_get_locale();
	return loc && loc[0] == 'd' && loc[1] == 'e';
}

static inline const char *tr_source_name(void)
{
	return "Easy IRL Stream";
}

static inline const char *tr_api_token(void)
{
	return tr_is_de() ? "stools.cc API-Token" : "stools.cc API Token";
}

static inline const char *tr_login_button(void)
{
	return tr_is_de() ? "Bei stools.cc anmelden"
			  : "Sign in with stools.cc";
}

static inline const char *tr_api_info(void)
{
	return tr_is_de()
		? "Alle Einstellungen werden auf stools.cc/dashboard/plugin verwaltet.\n"
		  "1. Klicke oben auf den Button um stools.cc zu \xc3\xb6""ffnen\n"
		  "2. Erstelle einen Token und kopiere ihn\n"
		  "3. F\xc3\xbc""ge ihn im API-Token-Feld ein"
		: "All settings are managed at stools.cc/dashboard/plugin\n"
		  "1. Click the button above to open stools.cc\n"
		  "2. Create a token and copy it\n"
		  "3. Paste it into the API Token field";
}

static inline const char *tr_tools_menu_help(void)
{
	return tr_is_de() ? "Easy IRL Stream - Hilfe"
			  : "Easy IRL Stream - Help";
}

static inline const char *tr_tools_menu_stats(void)
{
	return "Easy IRL Stream - Stream Monitor";
}
