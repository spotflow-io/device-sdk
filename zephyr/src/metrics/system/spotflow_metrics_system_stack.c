/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_system_stack.h"
#include "spotflow_metrics_system.h"
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_types.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_DECLARE(spotflow_metrics_system, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

static struct spotflow_metric_int *g_stack_metric;
static struct spotflow_metric_float *g_stack_used_metric;

#ifndef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS
static struct k_thread *g_tracked_threads[CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_MAX_THREADS];
static struct k_mutex g_tracked_threads_mutex;
static bool g_tracked_threads_initialized;
#endif

static void report_thread_stack(const struct k_thread *thread, void *user_data);

int spotflow_metrics_system_stack_init(void)
{
	uint16_t max_threads = CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_MAX_THREADS;

	g_stack_metric = spotflow_register_metric_int_with_labels(
		"thread_stack_free_bytes", SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL, max_threads, 1);
	if (!g_stack_metric) {
		LOG_ERR("Failed to register stack free metric");
		return -ENOMEM;
	}

	g_stack_used_metric = spotflow_register_metric_float_with_labels(
		"thread_stack_used_percent", SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL, max_threads, 1);
	if (!g_stack_used_metric) {
		LOG_ERR("Failed to register stack used percent metric");
		return -ENOMEM;
	}

#ifndef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS
	k_mutex_init(&g_tracked_threads_mutex);
	for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_MAX_THREADS; i++) {
		g_tracked_threads[i] = NULL;
	}
	g_tracked_threads_initialized = true;
#endif

	LOG_INF("Registered stack metrics");
	return 1;
}

void spotflow_metrics_system_stack_collect(void)
{
	if (!g_stack_metric) {
		LOG_ERR("Stack metric not registered");
		return;
	}

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS
	k_thread_foreach(report_thread_stack, NULL);
#else
	if (!g_tracked_threads_initialized) {
		return;
	}

	k_mutex_lock(&g_tracked_threads_mutex, K_FOREVER);
	for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_MAX_THREADS; i++) {
		if (g_tracked_threads[i] != NULL) {
			report_thread_stack(g_tracked_threads[i], NULL);
		}
	}
	k_mutex_unlock(&g_tracked_threads_mutex);
#endif
}

int spotflow_metrics_system_stack_enable_thread(struct k_thread *thread)
{
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS
	ARG_UNUSED(thread);
	LOG_WRN("Stack tracking is automatic (ALL_THREADS mode)");
	return 0;
#else
	if (thread == NULL) {
		thread = k_current_get();
	}

	if (thread == NULL) {
		return -EINVAL;
	}

	if (!g_tracked_threads_initialized) {
		LOG_ERR("Stack metrics not initialized");
		return -EINVAL;
	}

	k_mutex_lock(&g_tracked_threads_mutex, K_FOREVER);

	for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_MAX_THREADS; i++) {
		if (g_tracked_threads[i] == thread) {
			k_mutex_unlock(&g_tracked_threads_mutex);
			return -EINVAL;
		}
	}

	for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_MAX_THREADS; i++) {
		if (g_tracked_threads[i] == NULL) {
			g_tracked_threads[i] = thread;
			k_mutex_unlock(&g_tracked_threads_mutex);
			LOG_INF("Added thread %p to stack tracking", (void *)thread);
			return 0;
		}
	}

	k_mutex_unlock(&g_tracked_threads_mutex);
	LOG_ERR("Maximum tracked threads limit reached");
	return -ENOMEM;
#endif
}

static void report_thread_stack(const struct k_thread *thread, void *user_data)
{
	ARG_UNUSED(user_data);

	if (thread == NULL) {
		return;
	}

	size_t unused_bytes;
	int rc = k_thread_stack_space_get(thread, &unused_bytes);
	if (rc != 0) {
		return;
	}

	size_t stack_size = thread->stack_info.size;
	size_t used_bytes = stack_size - unused_bytes;

	char thread_label[32];
#ifdef CONFIG_THREAD_NAME
	const char *name = k_thread_name_get((k_tid_t)thread);
	if (name != NULL && name[0] != '\0') {
		strncpy(thread_label, name, sizeof(thread_label) - 1);
		thread_label[sizeof(thread_label) - 1] = '\0';
	} else {
		snprintf(thread_label, sizeof(thread_label), "%p", (void *)thread);
	}
#else
	snprintf(thread_label, sizeof(thread_label), "%p", (void *)thread);
#endif

	struct spotflow_label labels[] = {{.key = "thread", .value = thread_label}};

	rc = spotflow_report_metric_int_with_labels(g_stack_metric, (int64_t)unused_bytes, labels, 1);
	if (rc < 0) {
		LOG_ERR("Failed to report stack free metric for %s: %d", thread_label, rc);
	}

	float used_percent = (float)used_bytes / (float)stack_size * 100.0f;
	rc = spotflow_report_metric_float_with_labels(g_stack_used_metric, used_percent, labels, 1);
	if (rc < 0) {
		LOG_ERR("Failed to report stack used percent metric for %s: %d", thread_label, rc);
	}

	/* Log using integer format to avoid float-to-double promotion (Zephyr convention) */
	int pct_int = (int)used_percent;
	int pct_frac = (int)((used_percent - pct_int) * 10);
	LOG_DBG("Stack: thread=%s, used=%d.%01d%%, free=%zu bytes", thread_label, pct_int, pct_frac, unused_bytes);
}
