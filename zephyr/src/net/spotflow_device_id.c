#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>

#include "spotflow_device_id.h"

/* Should be safe upper bound - used, for example, in Zephyr Shell */
#define ZEPHYR_DEVICE_ID_MAX_LENGTH 16

LOG_MODULE_REGISTER(spotflow_device_id, CONFIG_SPOTFLOW_PROCESSING_BACKEND_LOG_LEVEL);

/* Hexadecimal representation of the Zephyr device ID and a null terminator */
char spotflow_generated_device_id_buffer[(2 * ZEPHYR_DEVICE_ID_MAX_LENGTH) + 1];

char* cached_device_id = NULL;

char* __attribute__((weak)) spotflow_override_device_id()
{
	return NULL;
}

void spotflow_generate_device_id()
{
	if (strlen(spotflow_generated_device_id_buffer) > 0) {
		return;
	}

	uint8_t device_id[ZEPHYR_DEVICE_ID_MAX_LENGTH];

	ssize_t ret = hwinfo_get_device_id(device_id, ARRAY_SIZE(device_id));

	if (ret <= 0) {
		LOG_ERR("Failed to get Zephyr device ID (%d), using default", (int)ret);

		strncpy(spotflow_generated_device_id_buffer, "default_device_id",
			sizeof(spotflow_generated_device_id_buffer));
		return;
	}

	for (int i = 0; i < ret; i++) {
		snprintk(spotflow_generated_device_id_buffer + (2 * i), 3, "%02X", device_id[i]);
	}
}

char* spotflow_get_device_id()
{
	if (cached_device_id != NULL) {
		return cached_device_id;
	}

	char* device_id = spotflow_override_device_id();

	if (device_id == NULL) {
		device_id = CONFIG_SPOTFLOW_DEVICE_ID;
	}

	if (strlen(device_id) == 0) {
		spotflow_generate_device_id();
		device_id = spotflow_generated_device_id_buffer;
	}

	cached_device_id = device_id;

	LOG_INF("Using Spotflow device ID: %s", cached_device_id);

	return cached_device_id;
}
