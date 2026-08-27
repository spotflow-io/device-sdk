#ifndef SPOTFLOW_MINIMAL_METRICS_H_
#define SPOTFLOW_MINIMAL_METRICS_H_

#include "metrics/spotflow_metrics_types.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void spotflow_minimal_metrics_init(void);
int spotflow_minimal_metrics_process(uint8_t* workspace, size_t workspace_size);

/* Internal static-label API. Label strings remain owned by the caller. */
int spotflow_minimal_register_static_label_int(const char* name,
					       enum spotflow_agg_interval agg_interval,
					       const char* label_key, uint16_t max_series,
					       struct spotflow_metric_int** metric_out);
int spotflow_minimal_register_static_label_float(const char* name,
						 enum spotflow_agg_interval agg_interval,
						 const char* label_key, uint16_t max_series,
						 struct spotflow_metric_float** metric_out);
int spotflow_minimal_report_static_label_int(struct spotflow_metric_int* metric, int64_t value,
					     const char* label_value);
int spotflow_minimal_report_static_label_float(struct spotflow_metric_float* metric, float value,
					       const char* label_value);

/* Shared by the minimal system collectors to keep the backend at one mutex. */
void spotflow_minimal_metrics_lock(void);
void spotflow_minimal_metrics_unlock(void);
void spotflow_minimal_metrics_system_collect_if_due(int64_t now_ms);

int spotflow_metrics_system_enable_thread_stack_with_label(struct k_thread* thread,
							   const char* label);

int spotflow_minimal_reset_init(void);
void spotflow_minimal_reset_process(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_MINIMAL_METRICS_H_ */
