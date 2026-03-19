#pragma once
#include <Logging.h>

// Debug printf macro — maps to LOG_DBG for serial output
#ifdef ENABLE_SERIAL_LOG
#define DEBUG_PRINTF(fmt, ...) LOG_DBG("DBG", fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...) ((void)0)
#endif
