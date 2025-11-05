#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "protocol_examples_common.h"

#include "esp_log.h"
#include "spotflow.h"
#include <sys/param.h>

static const char* TAG = "spotflow_testing_coredump";

#define GPIO_INPUT_IO_0     4

// ISR handler — must be IRAM safe if registered with ISR service
static void IRAM_ATTR button_isr_handler(void* arg)
{
    esp_system_abort("Button pressed -> deliberate crash");
}


void app_main(void)
{
	ESP_LOGI(TAG, "[APP] Startup..");
	ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
	ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

	 gpio_config_t btn_conf = {
		.intr_type = GPIO_INTR_NEGEDGE,    // Interrupt on falling edge (button press)
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << GPIO_INPUT_IO_0,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // enable pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&btn_conf);

	gpio_install_isr_service(0);

    // Attach the ISR handler for the button
    gpio_isr_handler_add(GPIO_INPUT_IO_0, button_isr_handler, NULL);

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
	while (1) {
		// if(atomic_load(&mqtt_connected))
		{

			ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
		}
		vTaskDelay(pdMS_TO_TICKS(5000));
		// int a = 15 / 0;
		// ESP_LOGI(TAG, "%d", a);
	}
}
