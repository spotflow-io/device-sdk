#ifndef SPOTFLOW_COREDUMP_QUEUE_H
#define SPOTFLOW_COREDUMP_QUEUE_H

#include "logging/spotflow_log_queue.h"

#ifdef __cplusplus
extern "C" {
#endif
extern bool coredump_found;
int8_t queue_coredump_push(uint8_t* msg, size_t len);
bool queue_coredump_read(queue_msg_t* out);
void queue_coredump_free(queue_msg_t* msg);
void queue_coredump_init(void);

#ifdef __cplusplus
}
#endif

#endif