/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock implementations for unit testing metrics module in isolation.
 * These stubs replace the real network/CBOR dependencies.
 */

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>

/* Forward declare types from metrics module */
struct spotflow_metric_base;
struct metric_timeseries_state;
struct spotflow_label;

/*
 * Mock CBOR encoding - returns success and provides dummy encoded data
 *
 * In a real implementation, this would encode metrics to CBOR format.
 * For unit tests, we just allocate a small buffer to simulate success.
 */
int spotflow_metrics_cbor_encode(struct spotflow_metric_base *metric,
				 struct metric_timeseries_state *ts,
				 int64_t timestamp_ms, uint64_t seq_num,
				 uint8_t **cbor_data, size_t *cbor_len)
{
	/* Allocate minimal buffer to simulate encoding */
	*cbor_data = k_malloc(64);
	if (*cbor_data == NULL) {
		return -ENOMEM;
	}
	*cbor_len = 64;
	return 0;
}

int spotflow_metrics_cbor_encode_no_aggregation(struct spotflow_metric_base *metric,
						const struct spotflow_label *labels,
						uint8_t label_count, int64_t value_int,
						float value_float, int64_t timestamp_ms,
						uint64_t seq_num, uint8_t **cbor_data,
						size_t *cbor_len)
{
	*cbor_data = k_malloc(64);
	if (*cbor_data == NULL) {
		return -ENOMEM;
	}
	*cbor_len = 64;
	return 0;
}

/*
 * Mock message queue for metrics
 *
 * In the real implementation, this queue is defined in spotflow_metrics_net.c
 * For unit tests, we define it here with the same structure.
 */
struct spotflow_mqtt_metrics_msg {
	uint8_t *payload;
	size_t len;
};

K_MSGQ_DEFINE(g_spotflow_metrics_msgq, sizeof(struct spotflow_mqtt_metrics_msg *),
	      CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE, sizeof(void *));

/*
 * Mock network initialization
 */
void spotflow_metrics_net_init(void)
{
	/* No-op for unit tests */
}
