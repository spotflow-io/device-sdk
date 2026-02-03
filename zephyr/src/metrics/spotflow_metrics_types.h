/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPOTFLOW_METRICS_TYPES_H_
#define SPOTFLOW_METRICS_TYPES_H_

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Aggregation interval enumeration
 *
 * Maps to ISO 8601 duration strings as specified in the ingestion protocol.
 */
enum spotflow_agg_interval {
	SPOTFLOW_AGG_INTERVAL_NONE = 0,   /* PT0S - No aggregation */
	SPOTFLOW_AGG_INTERVAL_1MIN = 1,   /* PT1M - 1 minute */
	SPOTFLOW_AGG_INTERVAL_1HOUR = 3,  /* PT1H - 1 hour */
	SPOTFLOW_AGG_INTERVAL_1DAY = 4    /* P1D - 1 day */
};

/**
 * @brief Metric value type enumeration
 */
enum spotflow_metric_type {
	SPOTFLOW_METRIC_TYPE_INT = 0,
	SPOTFLOW_METRIC_TYPE_FLOAT = 1
};

/**
 * @brief Label key-value pair
 *
 * Both key and value are null-terminated strings.
 * Maximum lengths: key=16 chars, value=32 chars (including null terminator).
 */
struct spotflow_label {
	const char *key;
	const char *value;
};

/**
 * @brief Internal label storage (copied strings)
 *
 * Sizes per design specification:
 * - Key: max 16 characters (SPOTFLOW_MAX_LABEL_KEY_LEN)
 * - Value: max 32 characters (SPOTFLOW_MAX_LABEL_VALUE_LEN)
 */
#define SPOTFLOW_MAX_LABEL_KEY_LEN    16
#define SPOTFLOW_MAX_LABEL_VALUE_LEN  32

struct metric_label_storage {
	char key[SPOTFLOW_MAX_LABEL_KEY_LEN];
	char value[SPOTFLOW_MAX_LABEL_VALUE_LEN];
};

/**
 * @brief Time series state (internal use)
 *
 * Tracks aggregation state for one unique label combination.
 */
struct metric_timeseries_state {
	/* Label identification */
	uint8_t label_count;               /* Number of labels (0 for label-less) */
	struct metric_label_storage labels[CONFIG_SPOTFLOW_METRICS_MAX_LABELS_PER_METRIC];

	/* Aggregation state */
	union {
		int64_t sum_int;
		float sum_float;
	};
	union {
		int64_t min_int;
		float min_float;
	};
	union {
		int64_t max_int;
		float max_float;
	};
	uint64_t count;                    /* Number of values aggregated */
	bool sum_truncated;                /* Sum overflow flag */

	bool active;                       /* Slot in use */
};

/**
 * @brief Base metric structure (internal use)
 *
 * Common fields shared by int and float metrics.
 */
struct spotflow_metric_base {
	/* Metric identification */
	char name[256];                    /* Normalized metric name */
	enum spotflow_metric_type type;    /* INT or FLOAT */
	enum spotflow_agg_interval agg_interval;

	/* Labeled metric configuration */
	uint16_t max_timeseries;           /* Maximum time series (1 for label-less) */
	uint8_t max_labels;                /* Maximum labels per report */

	/* Message sequencing */
	uint64_t sequence_number;          /* Incremented per message transmitted */

	/* Synchronization */
	struct k_mutex lock;               /* Protects metric state */

	/* Aggregator context (allocated dynamically) */
	void *aggregator_context;          /* Points to metric_aggregator_context */
};

/**
 * @brief Integer metric structure (internal use)
 *
 * Type-specific wrapper ensuring only int64_t values can be reported.
 */
struct spotflow_metric_int {
	struct spotflow_metric_base base;
};

/**
 * @brief Float metric structure (internal use)
 *
 * Type-specific wrapper ensuring only float values can be reported.
 */
struct spotflow_metric_float {
	struct spotflow_metric_base base;
};

/**
 * @brief Aggregator context per metric (internal use)
 */
struct metric_aggregator_context {
	struct spotflow_metric_base *metric;
	struct metric_timeseries_state *timeseries;
	uint16_t timeseries_count;         /* Current number of active time series */
	uint16_t timeseries_capacity;      /* Max (from metric->max_timeseries) */

	/* Timer scope: ONE timer per metric (not per time series) */
	/* All time series of this metric share the same aggregation window */
	/* When timer expires, all active time series generate messages with their counts */
	struct k_work_delayable aggregation_work;
	bool timer_started;                /* Flag to prevent timer restart race */
};

/**
 * @brief MQTT message structure (internal use)
 */
struct spotflow_mqtt_metrics_msg {
	uint8_t *payload;                  /* CBOR-encoded message */
	size_t len;                        /* Payload length */
};

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_TYPES_H_ */
