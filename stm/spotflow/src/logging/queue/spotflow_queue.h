#ifndef LOGGING_SPOTFLOW_QUEUE_H
#define LOGGING_SPOTFLOW_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "spotflow_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SpotflowFrame {
    uint16_t len;
    uint8_t payload[SPOTFLOW_MAX_FRAME_SIZE];
} SpotflowFrame;

bool spotflow_queue_init(void);
SpotflowFrame *spotflow_queue_acquire_frame(uint32_t timeout_ms);
void spotflow_queue_release_frame(SpotflowFrame *frame);
bool spotflow_queue_submit(SpotflowFrame *frame, uint32_t timeout_ms);
SpotflowFrame *spotflow_queue_consume(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* LOGGING_SPOTFLOW_QUEUE_H */