#ifndef LOGGING_SPOTFLOW_TRANSPORT_UART_H
#define LOGGING_SPOTFLOW_TRANSPORT_UART_H

#include <stdbool.h>
#include <stdint.h>

#include "../queue/spotflow_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

bool spotflow_transport_send_frame(const SpotflowFrame *frame);

#ifdef __cplusplus
}
#endif

#endif /* LOGGING_SPOTFLOW_TRANSPORT_UART_H */
