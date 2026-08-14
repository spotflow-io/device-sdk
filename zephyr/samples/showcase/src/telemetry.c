#include "showcase.h"

#include "metrics/spotflow_metrics_backend.h"
#include "metrics/system/spotflow_metrics_system_stack.h"

#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(showcase_telemetry, LOG_LEVEL_DBG);

static struct spotflow_metric_int* cycle_metric;
static struct spotflow_metric_float* temperature_metric;
static struct spotflow_metric_int* fault_metric;
static struct spotflow_metric_float* operation_duration_metric;
static struct spotflow_metric_int* periodic_event_metric;
static int64_t cycle;

int showcase_telemetry_init(void)
{
	int rc = spotflow_register_metric_int("showcase_cycle", SPOTFLOW_AGG_INTERVAL_1MIN,
					      &cycle_metric);

	if (rc < 0) {
		return rc;
	}

	rc = spotflow_register_metric_float("showcase_temperature_celsius",
					    SPOTFLOW_AGG_INTERVAL_NONE, &temperature_metric);
	if (rc < 0) {
		return rc;
	}

	rc = spotflow_register_metric_int_with_labels("showcase_fault", SPOTFLOW_AGG_INTERVAL_NONE,
						      4, 1, &fault_metric);
	if (rc < 0) {
		return rc;
	}

	rc = spotflow_register_metric_float_with_labels("showcase_operation_duration_ms",
							SPOTFLOW_AGG_INTERVAL_1MIN, 4, 2,
							&operation_duration_metric);
	if (rc < 0) {
		return rc;
	}

	rc = spotflow_register_metric_int("showcase_periodic_event", SPOTFLOW_AGG_INTERVAL_NONE,
					  &periodic_event_metric);
	if (rc < 0) {
		return rc;
	}

	rc = spotflow_metrics_system_stack_enable_thread(k_current_get());
	if (rc < 0) {
		LOG_WRN("Failed to enable main-thread stack metrics: %d", rc);
	}

	LOG_INF("Registered integer, float, labeled, event, and system metrics");
	return 0;
}

void showcase_telemetry_emit(void)
{
	static const char* const operations[] = { "read", "write" };
	static const char* const outcomes[] = { "ok", "retry" };
	const char* operation = operations[cycle % ARRAY_SIZE(operations)];
	const char* outcome = outcomes[(cycle / 2) % ARRAY_SIZE(outcomes)];
	struct spotflow_label operation_labels[] = {
		{ .key = "operation", .value = operation },
		{ .key = "outcome", .value = outcome },
	};
	float temperature = 20.0f + ((float)(sys_rand32_get() % 500U) / 100.0f);
	float duration = 10.0f + ((float)(sys_rand32_get() % 1900U) / 10.0f);

	cycle++;
	LOG_DBG("Showcase debug cycle %lld", cycle);
	LOG_INF("Showcase cycle %lld, temperature %.2f C", cycle, (double)temperature);
	if ((cycle % 4) == 0) {
		LOG_WRN("Demonstration warning on cycle %lld", cycle);
	}
	if ((cycle % 10) == 0) {
		LOG_ERR("Demonstration error on cycle %lld", cycle);
	}

	(void)spotflow_report_metric_int(cycle_metric, cycle);
	(void)spotflow_report_metric_float(temperature_metric, temperature);
	(void)spotflow_report_metric_float_with_labels(operation_duration_metric, duration,
						       operation_labels,
						       ARRAY_SIZE(operation_labels));

	if ((cycle % 6) == 0) {
		struct spotflow_label fault_label = { .key = "code", .value = "timeout" };

		(void)spotflow_report_metric_int_with_labels(fault_metric, cycle, &fault_label, 1);
		(void)spotflow_report_event_with_labels(fault_metric, &fault_label, 1);
	}

	if ((cycle % 3) == 0) {
		(void)spotflow_report_event(periodic_event_metric);
	}
}
