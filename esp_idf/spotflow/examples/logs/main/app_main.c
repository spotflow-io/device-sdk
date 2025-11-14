#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"

#include "esp_log.h"
#include "spotflow.h"
#include <sys/param.h>

static const char* TAG = "spotflow_testing";

void app_main(void)
{
	ESP_LOGI(TAG, "[APP] Startup..");
	ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
	ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

	esp_log_level_set("*", ESP_LOG_INFO);
	esp_log_level_set("spotflow_testing",
			  ESP_LOG_VERBOSE); //Setting the current tag to maximum log level

	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	/* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
	ESP_ERROR_CHECK(example_connect());

	spotflow_init();

	int test_num = 1;
	while (1) {
		// if(atomic_load(&mqtt_connected))
		{
			ESP_LOGI(TAG, "Info log message works");
			ESP_LOGD(TAG, "Debug log message works");
			ESP_LOGE(TAG, "Debug log message works");
			ESP_LOGW(TAG, "Warning log message works");
			ESP_LOGV(TAG, "Verbose log message works");

			//Testing long log messages. With differenet types
			ESP_LOGI(TAG,
				 "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abc"
				 "defghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdef"
				 "ghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghi"
				 "jklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijkl"
				 "mnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmno"
				 "pqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqr"
				 "stuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");

			ESP_LOGW(TAG, "Test Number. %d", test_num++);
			ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes",
				 esp_get_free_heap_size());
		}
		vTaskDelay(5000/portTICK_PERIOD_MS); // Delay for 5s
	}
}
