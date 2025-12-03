#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/logging/log.h>

#include "metrics/spotflow_metrics.h"

LOG_MODULE_REGISTER(metrics, LOG_LEVEL_INF);

extern struct sys_heap _system_heap; /* provided by Zephyr */

static struct k_work_delayable metrics_work;
static t_metric *m_heap_free;
static t_metric *m_heap_used;
static t_metric *m_mem_peak;
static t_metric *m_cpu_util;

static int sample_cpu_util(int cpu_id)
{
	/* You can swap to cpu_load_metric_get(cpu_id) if preferred */
	static uint64_t prev_exec[CONFIG_MP_MAX_NUM_CPUS];
	static uint64_t prev_total[CONFIG_MP_MAX_NUM_CPUS];
	struct k_thread_runtime_stats stats;

	if (k_thread_runtime_stats_cpu_get(cpu_id, &stats) != 0) {
		return -EIO;
	}

	uint64_t d_exec = stats.execution_cycles - prev_exec[cpu_id];
	uint64_t d_total = stats.total_cycles - prev_total[cpu_id];
	prev_exec[cpu_id] = stats.execution_cycles;
	prev_total[cpu_id] = stats.total_cycles;

	if (d_total == 0) {
		return 0;
	}

	return (int)((100 * d_exec) / d_total); /* 0–100 % */
}

static void metrics_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	struct sys_memory_stats heap_stats;
	if (sys_heap_runtime_stats_get(&_system_heap, &heap_stats) == 0) {
		report_metric_int(m_heap_free, NULL, 0, (int64_t)heap_stats.free_bytes);
		report_metric_int(m_heap_used, NULL, 0, (int64_t)heap_stats.allocated_bytes);
		report_metric_int(m_mem_peak, NULL, 0, (int64_t)heap_stats.max_allocated_bytes);
	}

	int cpu0 = sample_cpu_util(0);
	if (cpu0 >= 0) {
		report_metric_int(m_cpu_util, NULL, 0, cpu0);
	}

	k_work_reschedule(&metrics_work, K_SECONDS(5));
}

int spotflow_metrics_init(void)
{
	spotflow_metrics_pipeline_init();

	/* No dimensions, single timeseries each */
	m_heap_free = register_metric_int("heap_free_bytes", 1, 0, false);
	m_heap_used = register_metric_int("heap_used_bytes", 1, 0, false);
	m_mem_peak = register_metric_int("heap_peak_bytes", 1, 0, false);
	m_cpu_util = register_metric_int("cpu_util_percent", 1, 0, false);

	if (!m_heap_free || !m_heap_used || !m_mem_peak || !m_cpu_util) {
		return -ENOMEM;
	}

	k_work_init_delayable(&metrics_work, metrics_work_handler);
	k_work_schedule(&metrics_work, K_SECONDS(1)); /* warm-up: first delta */
	return 0;
}
