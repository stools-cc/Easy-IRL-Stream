#pragma once

#include <obs-module.h>

#ifndef PLUGIN_NAME
#define PLUGIN_NAME "Easy IRL Stream"
#endif

#ifdef DEBUG_BUILD
#define dbg_log(...) blog(__VA_ARGS__)
#else
#define dbg_log(...) ((void)0)
#endif
