#ifndef SPOTFLOW_LOG_H
#define SPOTFLOW_LOG_H

#include "spotflow.h"

struct message_metadata{
	uint8_t severity;
	unsigned long uptime_ms;
	size_t sequence_number;
	const char* source;
};

int spotflow_log_backend(const char *fmt, va_list args);

#if CONFIG_SPOTFLOW_DEBUG_MESSAGE_TERMINAL
    #define SPOTFLOW_LOG(fmt, ...) printf("[SPOTFLOW] " fmt, ##__VA_ARGS__)
#else
    #define SPOTFLOW_LOG(fmt, ...) do {} while(0)
#endif

#endif