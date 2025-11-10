#ifndef SPOTFLOW_LOG_BACKEND_H
#define SPOTFLOW_LOG_BACKEND_H

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

#if CONFIG_SPOTFLOW_DEBUG_MESSAGE_TERMINAL
#define SPOTFLOW_LOG(fmt, ...) printf("[SPOTFLOW] " fmt, ##__VA_ARGS__)
#else
#define SPOTFLOW_LOG(fmt, ...) \
	do {                   \
	} while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif