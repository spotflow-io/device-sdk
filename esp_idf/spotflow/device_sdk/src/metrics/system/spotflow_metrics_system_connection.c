#include "metrics/system/spotflow_metrics_system.h"
#include "logging/spotflow_log_backend.h"

#include <errno.h>
#include <stdint.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

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

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE
#include "metrics/system/spotflow_reset_helper.h"
#endif

static TimerHandle_t g_collection_timer = NULL;

/*
 * Initialization state (thread-safe with C11 atomics):
 * 0 = not initialized
 * 1 = initialization in progress
 * 2 = fully initialized
 */
static atomic_int g_system_metrics_init_state = ATOMIC_VAR_INIT(0);

static void collection_timer_handler(TimerHandle_t xTimer);

int spotflow_metrics_system_init(void)
{
    int expected = 2;
    if (atomic_load(&g_system_metrics_init_state) == 2) {
        return 0;
    }

    expected = 0;
    if (!atomic_compare_exchange_strong(&g_system_metrics_init_state, &expected, 1)) {
        while (atomic_load(&g_system_metrics_init_state) != 2) {
            taskYIELD();
        }
        return 0;
    }

    SPOTFLOW_DEBUG("Initializing system metrics auto-collection");

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
    spotflow_report_reboot_reason();
#endif

    g_collection_timer = xTimerCreate("collection_timer",
                                      pdMS_TO_TICKS(CONFIG_SPOTFLOW_METRICS_SYSTEM_COLLECTION_INTERVAL * 1000),
                                      pdTRUE, NULL, collection_timer_handler);
    if (!g_collection_timer) {
        return -ENOMEM;
    }
    xTimerStart(g_collection_timer, 0);

    atomic_store(&g_system_metrics_init_state, 2);

    SPOTFLOW_LOG(
        "System metrics initialized: %d metrics registered, collection=%ds, aggregation=%ds",
        registered_count, CONFIG_SPOTFLOW_METRICS_SYSTEM_COLLECTION_INTERVAL,
        CONFIG_SPOTFLOW_METRICS_SYSTEM_AGGREGATION_INTERVAL);

    return 0;
}

void spotflow_metrics_system_report_connection_state(bool connected)
{
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
    if (atomic_load(&g_system_metrics_init_state) != 2) {
        return;
    }
    spotflow_metrics_system_connection_report(connected);
#endif
}

static void collection_timer_handler(TimerHandle_t xTimer)
{
    SPOTFLOW_DEBUG("Collecting system metrics...");

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
}

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK
int spotflow_metrics_system_enable_thread_stack(TaskHandle_t thread)
{
    return spotflow_metrics_system_stack_enable_thread(thread);
}
#endif