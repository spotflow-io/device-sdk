#include "metrics/spotflow_system_metrics.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/cpu_load.h>

#include "metrics/spotflow_metrics.h"

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

extern struct sys_heap _system_heap; /* provided by Zephyr */

static bool system_metrics_running;
static t_metric* m_heap_free;
static t_metric* m_heap_used;
static t_metric* m_heap_peak;
static t_metric* m_cpu_util;
static struct k_work_delayable system_metrics_work;

static int sample_cpu_util(int cpu_id)
{
	int load = cpu_load_metric_get(cpu_id);
	if (load < 0) {
		return load;
	}
	return load;
}

static void system_metrics_work_handler(struct k_work* work)
{
	ARG_UNUSED(work);

	struct sys_memory_stats heap_stats;
	if (sys_heap_runtime_stats_get(&_system_heap, &heap_stats) == 0) {
		report_metric_int(m_heap_free, NULL, 0, (int64_t)heap_stats.free_bytes);
		report_metric_int(m_heap_used, NULL, 0, (int64_t)heap_stats.allocated_bytes);
		report_metric_int(m_heap_peak, NULL, 0, (int64_t)heap_stats.max_allocated_bytes);
	}

	int cpu0 = sample_cpu_util(0);
	if (cpu0 >= 0) {
		report_metric_int(m_cpu_util, NULL, 0, cpu0);
	}

	k_work_reschedule(&system_metrics_work, K_SECONDS(CONFIG_SPOTFLOW_SYSTEM_METRICS_INTERVAL_SEC));
}

int spotflow_system_metrics_start(void)
{
	if (system_metrics_running) {
		return 0;
	}

	spotflow_metrics_pipeline_init();

	/* No dimensions, single timeseries each */
	m_heap_free = register_metric_int("heap_free_bytes", 1, 0, false);
	m_heap_used = register_metric_int("heap_used_bytes", 1, 0, false);
	m_heap_peak = register_metric_int("heap_peak_bytes", 1, 0, false);
	m_cpu_util = register_metric_int("cpu_util_percent", 1, 0, false);

	if (!m_heap_free || !m_heap_used || !m_heap_peak || !m_cpu_util) {
		return -ENOMEM;
	}

	k_work_init_delayable(&system_metrics_work, system_metrics_work_handler);
	k_work_schedule(&system_metrics_work, K_SECONDS(1));

	system_metrics_running = true;
	return 0;
}

int spotflow_system_metrics_stop(void)
{
	if (!system_metrics_running) {
		return 0;
	}
	k_work_cancel_delayable(&system_metrics_work);
	system_metrics_running = false;
	return 0;
}
