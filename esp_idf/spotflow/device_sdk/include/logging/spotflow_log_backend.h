#ifndef SPOTFLOW_LOG_BACKEND_H
#define SPOTFLOW_LOG_BACKEND_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct message_metadata {
	uint8_t severity;
	unsigned long uptime_ms;
	size_t sequence_number;
	const char* source;
};

int spotflow_log_backend(const char* fmt, va_list args);
void spotflow_log_backend_try_set_runtime_filter(uint8_t level);

#ifdef __cplusplus
}
#endif

#endif
