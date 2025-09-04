#ifndef SPOTFLOW_COREDUMPS_BACKEND_H
#define SPOTFLOW_COREDUMPS_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct spotflow_mqtt_coredumps_msg {
	uint8_t* payload;
	size_t len;
	bool coredump_last_chunk;
};

extern struct k_msgq g_spotflow_core_dumps_msgq;

void spotflow_coredump_sent();

#ifdef __cplusplus
}
#endif

#endif //SPOTFLOW_COREDUMPS_BACKEND_H
