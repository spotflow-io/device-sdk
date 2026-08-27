#include "spotflow_minimal_metrics.h"

#include "metrics/system/spotflow_metrics_system.h"

#include <errno.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP
#include "metrics/system/spotflow_metrics_system_heap.h"
#endif
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK
#include "metrics/system/spotflow_metrics_system_network.h"
#endif
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU
#include "metrics/system/spotflow_metrics_system_cpu.h"
#endif
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
#include "metrics/system/spotflow_metrics_system_connection.h"
#endif
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK
#include "metrics/system/spotflow_metrics_system_stack.h"
#endif

LOG_MODULE_DECLARE(spotflow_metrics, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

static int64_t g_collection_deadline_ms;
static bool g_system_initialized;
static bool g_system_init_attempted;
static int g_system_init_result;
static K_MUTEX_DEFINE(g_system_init_lock);

int spotflow_metrics_system_init(void)
{
	k_mutex_lock(&g_system_init_lock, K_FOREVER);
	if (g_system_init_attempted) {
		int result = g_system_init_result;
		k_mutex_unlock(&g_system_init_lock);
		return result;
	}

	int registered = 0;
	int rc = 0;
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK
	/* The generic network collector uses labels and is unavailable in minimal mode. */
	rc = -ENOTSUP;
	goto done;
#endif
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP
	rc = spotflow_metrics_system_heap_init();
	if (rc < 0) {
		goto done;
	}
	registered += rc;
#endif
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU
	rc = spotflow_metrics_system_cpu_init();
	if (rc < 0) {
		goto done;
	}
	registered += rc;
#endif
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
	rc = spotflow_metrics_system_connection_init();
	if (rc < 0) {
		goto done;
	}
	registered += rc;
#endif
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK
	rc = spotflow_metrics_system_stack_init();
	if (rc < 0) {
		goto done;
	}
	registered += rc;
#endif
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE
	rc = spotflow_minimal_reset_init();
	if (rc < 0) {
		goto done;
	}
#endif
	ARG_UNUSED(rc);

	spotflow_minimal_metrics_lock();
	g_collection_deadline_ms = k_uptime_get() +
		(int64_t)CONFIG_SPOTFLOW_METRICS_SYSTEM_COLLECTION_INTERVAL * MSEC_PER_SEC;
	g_system_initialized = true;
	spotflow_minimal_metrics_unlock();
	LOG_INF("Minimal system metrics initialized: %d metrics", registered);
	rc = 0;

done:
	g_system_init_attempted = true;
	g_system_init_result = rc;
	k_mutex_unlock(&g_system_init_lock);
	return rc;
}

void spotflow_metrics_system_report_connection_state(bool connected)
{
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
	spotflow_minimal_metrics_lock();
	bool initialized = g_system_initialized;
	spotflow_minimal_metrics_unlock();
	if (initialized) {
		spotflow_metrics_system_connection_report(connected);
	}
#else
	ARG_UNUSED(connected);
#endif
}

void spotflow_minimal_metrics_system_collect_if_due(int64_t now_ms)
{
	spotflow_minimal_metrics_lock();
	if (!g_system_initialized || now_ms < g_collection_deadline_ms) {
		spotflow_minimal_metrics_unlock();
		return;
	}
	g_collection_deadline_ms =
		now_ms + (int64_t)CONFIG_SPOTFLOW_METRICS_SYSTEM_COLLECTION_INTERVAL * MSEC_PER_SEC;
	spotflow_minimal_metrics_unlock();

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP
	spotflow_metrics_system_heap_collect();
#endif
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU
	spotflow_metrics_system_cpu_collect();
#endif
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK
	spotflow_metrics_system_stack_collect();
#endif
}

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK
int spotflow_metrics_system_enable_thread_stack(struct k_thread* thread)
{
	ARG_UNUSED(thread);
	return -ENOTSUP;
}
#endif
