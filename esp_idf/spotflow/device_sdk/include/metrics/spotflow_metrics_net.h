#ifndef SPOTFLOW_METRICS_NET_H_
#define SPOTFLOW_METRICS_NET_H_

#include "spotflow_metrics_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize metrics network layer
 *
 * Initializes message queue for metrics transmission.
 * Called once during SDK initialization.
 */
void spotflow_metrics_net_init(void);

/**
 * @brief Poll and process one enqueued metric message
 *
 * Dequeues one message from the metrics queue and publishes via MQTT.
 * Called repeatedly by processor thread to drain the queue.
 *
 * @return 1 if message processed, 0 if queue empty, negative errno on failure
 */
int spotflow_poll_and_process_enqueued_metrics(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_NET_H_ */