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

ESP_EVENT_DEFINE_BASE(SPOTFLOW_EVENTS);

enum {
    SPOTFLOW_EVENT_TRIGGER_CRASH
};


// ISR handler — must be IRAM safe if registered with ISR service
static void IRAM_ATTR button_isr_handler(void* arg)
{
    esp_event_isr_post(SPOTFLOW_EVENTS, SPOTFLOW_EVENT_TRIGGER_CRASH, NULL, 0, NULL);
}

static void spotflow_event_handler(void *handler_arg, esp_event_base_t base,
                                   int32_t id, void *event_data)
{
    if (base == SPOTFLOW_EVENTS && id == SPOTFLOW_EVENT_TRIGGER_CRASH) {
        esp_system_abort("Deliberate crash triggered from event handler");
    }
}

void app_main(void)
{
	ESP_LOGI(TAG, "[APP] Startup..");
	ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
	ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Register our custom event handler
    ESP_ERROR_CHECK(esp_event_handler_register(SPOTFLOW_EVENTS, ESP_EVENT_ANY_ID,
                                               &spotflow_event_handler, NULL));

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

	/* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
	ESP_ERROR_CHECK(example_connect());

	spotflow_init();
	while (1) {
		ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
		vTaskDelay(5000/portTICK_PERIOD_MS); // Delay for 5s
	}
}
