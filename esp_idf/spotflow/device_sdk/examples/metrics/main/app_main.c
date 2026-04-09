#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_random.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"

#include "esp_log.h"
#include "spotflow.h"
#include "metrics/spotflow_metrics_backend.h"
#include <sys/param.h>

static const char* TAG = "spotflow_testing";
/* Metric handles - using type-specific handles */
static struct spotflow_metric_int* g_app_counter_metric;
static struct spotflow_metric_float* g_temperature_metric;
static struct spotflow_metric_float* g_request_duration_metric;

static int init_application_metrics(void);
static void report_counter_metric(void);
static void report_temperature_metric(void);
static void report_request_duration_metric(void);
static void temperature_task(void* arg);

void app_main(void)
{
	ESP_LOGI(TAG, "[APP] Startup..");
	ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
	ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

	esp_log_level_set("*", ESP_LOG_INFO);

	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	/* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
	ESP_ERROR_CHECK(example_connect());

	spotflow_init();
	int rc = init_application_metrics();
    if (rc < 0) {
        ESP_LOGI(TAG,"Failed to initialize application metrics");
        return;
    }
	 /* Create temperature task */
    xTaskCreate(temperature_task, "temperature_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG,"Starting metric reporting...");

    for (int iteration = 0; iteration < 100; iteration++) {
        ESP_LOGI(TAG,"=== Iteration %d ===", iteration);

        report_counter_metric();

        /* Simulate multiple HTTP requests */
        for (int req = 0; req < 3; req++) {
            report_request_duration_metric();
        }

        if (iteration % 10 == 0) {
            ESP_LOGI(TAG,"Periodic health check at iteration %d", iteration);
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }

	int test_num = 1;
	while (1) {
		// {
		// 	ESP_LOGI(TAG, "Info log message works");
		// 	ESP_LOGD(TAG, "Debug log message works");
		// 	ESP_LOGE(TAG, "Error log message works");
		// 	ESP_LOGW(TAG, "Warning log message works");
		// 	ESP_LOGV(TAG, "Verbose log message works");

		// 	//Testing long log messages. With differenet types
		// 	ESP_LOGI(TAG,
		// 		 "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abc"
		// 		 "defghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdef"
		// 		 "ghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghi"
		// 		 "jklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijkl"
		// 		 "mnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmno"
		// 		 "pqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqr"
		// 		 "stuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");

		// 	ESP_LOGW(TAG, "Test Number. %d", test_num++);
		// 	ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes",
		// 		 esp_get_free_heap_size());
		// }
		vTaskDelay(50000/portTICK_PERIOD_MS); // Delay for 50s
	}
}


/* ========================= */

static int init_application_metrics(void)
{
    int rc;

    rc = spotflow_register_metric_int("app_counter",
                                     SPOTFLOW_AGG_INTERVAL_1MIN,
                                     &g_app_counter_metric);
    if (rc < 0) {
        ESP_LOGI(TAG,"Failed to register app_counter metric: %d", rc);
        return rc;
    }

    ESP_LOGI(TAG,"Registered metric: app_counter");

    rc = spotflow_register_metric_float_with_labels(
        "http_request_duration_ms",
        SPOTFLOW_AGG_INTERVAL_1MIN,
        18,
        3,
        &g_request_duration_metric);

    if (rc < 0) {
        ESP_LOGI(TAG,"Failed to register request_duration metric: %d", rc);
        return rc;
    }

    ESP_LOGI(TAG,"Registered metric: http_request_duration_ms");

    return 0;
}

/* ========================= */

static void report_counter_metric(void)
{
    static int counter = 0;
    counter += 10;

    int rc = spotflow_report_metric_int(g_app_counter_metric, counter);
    if (rc < 0) {
        // ESP_LOGI(TAG,"Failed to report counter metric: %d", rc);
    } else {
        // ESP_LOGD(TAG,"Reported counter: %d", counter);
    }
}

/* ========================= */

static void report_temperature_metric(void)
{
    double temperature = 20.0 + ((double)(esp_random() % 500) / 100.0);

    int rc = spotflow_report_metric_float(g_temperature_metric, temperature);
    if (rc < 0) {
        ESP_LOGI(TAG,"Failed to report temperature: %d", rc);
    } else {
        ESP_LOGI(TAG,"Reported temperature: %.2f C", temperature);
    }
}

/* ========================= */

static void temperature_task(void* arg)
{
    int rc = spotflow_register_metric_float("temperature_celsius",
                                            SPOTFLOW_AGG_INTERVAL_NONE,
                                            &g_temperature_metric);

    if (rc < 0) {
        ESP_LOGI(TAG,"Failed to register temperature metric: %d", rc);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG,"Registered metric: temperature_celsius");

    while (true) {
        report_temperature_metric();
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/* ========================= */

static void report_request_duration_metric(void)
{
    const char* endpoints[] = { "/api/users", "/api/products", "/health" };
    const char* methods[] = { "GET", "POST" };
    const char* status_codes[] = { "200", "404", "500" };

    const char* endpoint = endpoints[esp_random() % 3];
    const char* method = methods[esp_random() % 2];
    const char* status = status_codes[esp_random() % 3];

    double duration_ms = 10.0 + ((double)(esp_random() % 4900) / 10.0);

    struct spotflow_label labels[] = {
        { .key = "endpoint", .value = endpoint },
        { .key = "method", .value = method },
        { .key = "status", .value = status }
    };

    int rc = spotflow_report_metric_float_with_labels(
        g_request_duration_metric,
        duration_ms,
        labels,
        3);

    if (rc < 0) {
        ESP_LOGI(TAG,"Failed to report request duration: %d", rc);
    } else {
        ESP_LOGD(TAG,"Reported request: %s %s -> %s (%.2f ms)",
                       method, endpoint, status, duration_ms);
    }
}