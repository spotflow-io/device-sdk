/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_system.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP
#include "spotflow_metrics_system_heap.h"
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK
#include "spotflow_metrics_system_network.h"
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU
#include "spotflow_metrics_system_cpu.h"
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
#include "spotflow_metrics_system_connection.h"
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK
#include "spotflow_metrics_system_stack.h"
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE
#include "spotflow_reset_helper.h"
#endif

LOG_MODULE_REGISTER(spotflow_metrics_system, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

static struct k_work_delayable g_collection_work;

/*
 * Initialization state (use atomic for thread safety):
 * 0 = not initialized
 * 1 = initialization in progress
 * 2 = fully initialized
 */
static atomic_t g_system_metrics_init_state = ATOMIC_INIT(0);

static void collection_timer_handler(struct k_work *work);

int spotflow_metrics_system_init(void)
{
	if (atomic_get(&g_system_metrics_init_state) == 2) {
		return 0;
	}

	if (!atomic_cas(&g_system_metrics_init_state, 0, 1)) {
		while (atomic_get(&g_system_metrics_init_state) != 2) {
			k_yield();
		}
		return 0;
	}

	LOG_DBG("Initializing system metrics auto-collection");

	int registered_count = 0;
	int rc;

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP
	rc = spotflow_metrics_system_heap_init();
	if (rc < 0) {
		return rc;
	}
	registered_count += rc;
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK
	rc = spotflow_metrics_system_network_init();
	if (rc < 0) {
		return rc;
	}
	registered_count += rc;
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU
	rc = spotflow_metrics_system_cpu_init();
	if (rc < 0) {
		return rc;
	}
	registered_count += rc;
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
	rc = spotflow_metrics_system_connection_init();
	if (rc < 0) {
		return rc;
	}
	registered_count += rc;
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK
	rc = spotflow_metrics_system_stack_init();
	if (rc < 0) {
		return rc;
	}
	registered_count += rc;
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE
	report_reboot_reason();
#endif

	k_work_init_delayable(&g_collection_work, collection_timer_handler);
	k_work_schedule(&g_collection_work, K_SECONDS(CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL));

	atomic_set(&g_system_metrics_init_state, 2);

	LOG_INF("System metrics initialized: %d metrics registered, collection interval=%d seconds",
		registered_count, CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL);

	return 0;
}

void spotflow_metrics_system_report_connection_state(bool connected)
{
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
	if (atomic_get(&g_system_metrics_init_state) != 2) {
		return;
	}
	spotflow_metrics_system_connection_report(connected);
#endif
}

static void collection_timer_handler(struct k_work *work)
{
	LOG_DBG("Collecting system metrics...");

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP
	spotflow_metrics_system_heap_collect();
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK
	spotflow_metrics_system_network_collect();
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU
	spotflow_metrics_system_cpu_collect();
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK
	spotflow_metrics_system_stack_collect();
#endif

	k_work_schedule(&g_collection_work, K_SECONDS(CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL));
}

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK
int spotflow_metrics_system_enable_thread_stack(struct k_thread *thread)
{
	return spotflow_metrics_system_stack_enable_thread(thread);
}
#endif
