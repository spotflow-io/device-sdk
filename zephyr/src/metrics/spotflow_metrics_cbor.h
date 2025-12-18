/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPOTFLOW_METRICS_CBOR_H_
#define SPOTFLOW_METRICS_CBOR_H_

#include "spotflow_metrics_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encode metric message to CBOR format
 *
 * Memory Ownership Contract:
 * 1. Encoder allocates and returns pointer to caller
 * 2. Caller enqueues pointer to message queue
 * 3. If enqueue fails: caller MUST free immediately
 * 4. If enqueue succeeds: ownership transfers to processor thread
 * 5. Processor thread ALWAYS frees (success or failure)
 *
 * @param metric Metric base handle
 * @param ts Time series state to encode
 * @param timestamp_ms Device uptime in milliseconds when aggregation window closed
 * @param sequence_number Sequence number for this message (caller must increment atomically)
 * @param cbor_data Output: allocated CBOR buffer (caller must free if enqueue fails)
 * @param cbor_len Output: CBOR buffer length
 *
 * @return 0 on success, negative errno on failure
 *         -EINVAL: CBOR encoding failed
 *         -ENOMEM: Memory allocation failed
 */
int spotflow_metrics_cbor_encode(
	struct spotflow_metric_base *metric,
	struct metric_timeseries_state *ts,
	int64_t timestamp_ms,
	uint64_t sequence_number,
	uint8_t **cbor_data,
	size_t *cbor_len);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_CBOR_H_ */
