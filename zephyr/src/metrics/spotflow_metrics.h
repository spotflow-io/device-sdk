#ifndef SPOTFLOW_METRICS_H
#define SPOTFLOW_METRICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct spotflow_metric_dimension {
	const char* key;
	const char* value;
};

typedef struct spotflow_metric t_metric;

int spotflow_metrics_pipeline_init(void);

t_metric* register_metric_int(const char* name, uint8_t max_ts_count, uint8_t max_dimensions,
			      bool collect_samples);
t_metric* register_metric_float(const char* name, uint8_t max_ts_count, uint8_t max_dimensions,
				bool collect_samples);
t_metric* register_metric_simple(const char* name, bool collect_samples);

int report_metric_int(t_metric* metric, const struct spotflow_metric_dimension* dimensions,
		      size_t dimensions_count, int64_t value);
int report_metric_float(t_metric* metric, const struct spotflow_metric_dimension* dimensions,
			size_t dimensions_count, double value);

int spotflow_metrics_flush_now(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_H */
