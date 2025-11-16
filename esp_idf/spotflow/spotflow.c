#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "net/spotflow_mqtt.h"
#include "logging/spotflow_log_backend.h"
#include "logging/spotflow_log_queue.h"
#ifdef CONFIG_SPOTFLOW_GENERATE_BUILD_ID
#include "buildid/spotflow_build_id.h"
#endif
#include "spotflow.h"

#ifdef CONFIG_ESP_COREDUMP_ENABLE
	#include "coredump/spotflow_coredump.h"
	#include "coredump/spotflow_coredump_queue.h"
#endif

vprintf_like_t original_vprintf = NULL;

/**
 * @brief 
 * @details To utilize the esp_log_set_vprintf function to expose the logs
 */
void spotflow_init(void)
{
	original_vprintf = esp_log_set_vprintf(spotflow_log_backend);

	spotflow_queue_init(); //Initilize the queue
	spotflow_mqtt_app_start(); // Calling the mqtt_start from the init function.
#ifdef CONFIG_ESP_COREDUMP_ENABLE
	if (spotflow_is_coredump_available()) {
		spotflow_queue_coredump_init();
		spotflow_coredump_backend();
	}
#endif
}