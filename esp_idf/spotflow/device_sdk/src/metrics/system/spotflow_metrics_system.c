#include "metrics/system/spotflow_metrics_system.h"
#include "metrics/system/spotflow_metrics_system_connection.h"
#include "logging/spotflow_log_backend.h"
#include "net/spotflow_mqtt.h"

#include <errno.h>
#include <stdatomic.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

static esp_timer_handle_t g_collection_timer = NULL;

/* Initialization state: 0 = not initialized, 1 = in progress, 2 = fully initialized */
static atomic_int g_system_metrics_init_state = ATOMIC_VAR_INIT(0);

static void collection_timer_handler(void* arg);

int spotflow_metrics_system_init(void)
{
	if (atomic_load(&g_system_metrics_init_state) == 2) {
		return 0;
	}

	int expected = 0;
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

	/* Create ESP-IDF one-shot timer */
	const esp_timer_create_args_t timer_args = { .callback = &collection_timer_handler,
						     .name = "system_metrics_timer" };
	esp_timer_create(&timer_args, &g_collection_timer);
	esp_timer_start_once(g_collection_timer,
			     CONFIG_SPOTFLOW_METRICS_SYSTEM_COLLECTION_INTERVAL * 1000000ULL);

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

static void collection_timer_handler(void* arg)
{
	SPOTFLOW_LOG("Collecting system metrics...");

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

	spotflow_mqtt_notify_action(SPOTFLOW_MQTT_NOTIFY_METRICS);
	/* Restart the timer */
	esp_timer_start_once(g_collection_timer,
			     CONFIG_SPOTFLOW_METRICS_SYSTEM_COLLECTION_INTERVAL * 1000000ULL);
}

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK
int spotflow_metrics_system_enable_thread_stack(TaskHandle_t task)
{
	return spotflow_metrics_system_stack_enable_thread(task);
}
#endif
