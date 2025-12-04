#ifndef SPOTFLOW_METRICS_CBOR_H
#define SPOTFLOW_METRICS_CBOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "metrics/spotflow_metrics.h"

struct spotflow_metric_stats_int {
	uint64_t count;
	int64_t sum;
	int64_t min;
	int64_t max;
};

struct spotflow_metric_stats_float {
	uint64_t count;
	double sum;
	double min;
	double max;
};

int spotflow_metrics_cbor_encode_int(const char* metric_name,
				     const struct spotflow_metric_dimension* dimensions,
				     size_t dimensions_count,
				     const struct spotflow_metric_stats_int* stats,
				     const int64_t* samples, size_t samples_count,
				     const char* aggregation_interval, uint64_t sequence_number,
				     uint8_t** cbor_data, size_t* cbor_len);

int spotflow_metrics_cbor_encode_float(const char* metric_name,
				       const struct spotflow_metric_dimension* dimensions,
				       size_t dimensions_count,
				       const struct spotflow_metric_stats_float* stats,
				       const double* samples, size_t samples_count,
				       const char* aggregation_interval, uint64_t sequence_number,
				       uint8_t** cbor_data, size_t* cbor_len);

#endif /* SPOTFLOW_METRICS_CBOR_H */
