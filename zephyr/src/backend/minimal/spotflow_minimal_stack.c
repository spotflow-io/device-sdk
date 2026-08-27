#include "spotflow_minimal_metrics.h"

#include "metrics/system/spotflow_metrics_system.h"
#include "metrics/system/spotflow_metrics_system_stack.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_metrics, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

struct tracked_thread {
	struct k_thread* thread;
	const char* label;
};

static struct tracked_thread g_threads[CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_MAX_THREADS];
static struct spotflow_metric_int* g_stack_free_metric;
static struct spotflow_metric_float* g_stack_used_metric;
static bool g_stack_initialized;

int spotflow_metrics_system_stack_init(void)
{
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS
	return -ENOTSUP;
#else
	int rc = spotflow_minimal_register_static_label_int(
		SPOTFLOW_METRIC_NAME_STACK_FREE, SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL, "thread",
		ARRAY_SIZE(g_threads), &g_stack_free_metric);
	if (rc < 0) {
		return rc;
	}
	rc = spotflow_minimal_register_static_label_float(
		SPOTFLOW_METRIC_NAME_STACK_USED_PERCENT, SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL,
		"thread", ARRAY_SIZE(g_threads), &g_stack_used_metric);
	if (rc < 0) {
		return rc;
	}
	spotflow_minimal_metrics_lock();
	g_stack_initialized = true;
	spotflow_minimal_metrics_unlock();
	return 2;
#endif
}

int spotflow_metrics_system_stack_enable_thread(struct k_thread* thread)
{
	ARG_UNUSED(thread);
	return -ENOTSUP;
}

int spotflow_metrics_system_enable_thread_stack_with_label(struct k_thread* thread,
							   const char* label)
{
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS
	ARG_UNUSED(thread);
	ARG_UNUSED(label);
	return -ENOTSUP;
#else
	if (thread == NULL) {
		thread = k_current_get();
	}
	if (thread == NULL || label == NULL || label[0] == '\0' ||
	    strlen(label) >= SPOTFLOW_MAX_LABEL_VALUE_LEN) {
		return -EINVAL;
	}

	spotflow_minimal_metrics_lock();
	if (!g_stack_initialized) {
		spotflow_minimal_metrics_unlock();
		return -EINVAL;
	}
	struct tracked_thread* available = NULL;
	for (size_t i = 0; i < ARRAY_SIZE(g_threads); ++i) {
		if (g_threads[i].thread == thread) {
			spotflow_minimal_metrics_unlock();
			return -EEXIST;
		}
		if (g_threads[i].thread == NULL && available == NULL) {
			available = &g_threads[i];
		}
	}
	if (available == NULL) {
		spotflow_minimal_metrics_unlock();
		return -ENOMEM;
	}
	available->thread = thread;
	available->label = label;
	spotflow_minimal_metrics_unlock();
	return 0;
#endif
}

void spotflow_metrics_system_stack_collect(void)
{
#ifndef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS
	for (size_t i = 0; i < ARRAY_SIZE(g_threads); ++i) {
		spotflow_minimal_metrics_lock();
		struct k_thread* thread = g_threads[i].thread;
		const char* label = g_threads[i].label;
		spotflow_minimal_metrics_unlock();
		if (thread == NULL) {
			continue;
		}

		size_t unused_bytes;
		if (k_thread_stack_space_get(thread, &unused_bytes) != 0) {
			continue;
		}
		size_t stack_size = thread->stack_info.size;
		int64_t unused = unused_bytes > INT64_MAX ? INT64_MAX : (int64_t)unused_bytes;
		(void)spotflow_minimal_report_static_label_int(g_stack_free_metric, unused, label);
		if (stack_size != 0) {
			float used =
				(float)(stack_size - unused_bytes) / (float)stack_size * 100.0f;
			(void)spotflow_minimal_report_static_label_float(g_stack_used_metric, used,
									 label);
		}
	}
#endif
}
