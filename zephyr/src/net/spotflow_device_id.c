#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>

#include "spotflow_device_id.h"

/* Should be safe upper bound - used, for example, in Zephyr Shell */
#define ZEPHYR_DEVICE_ID_MAX_LENGTH 16

LOG_MODULE_REGISTER(spotflow_device_id, CONFIG_SPOTFLOW_PROCESSING_BACKEND_LOG_LEVEL);

/* Hexadecimal representation of the Zephyr device ID (two characters per byte)
 * and a null terminator
 */
static char spotflow_generated_device_id_buffer[(2 * ZEPHYR_DEVICE_ID_MAX_LENGTH) + 1];
static const char* cached_device_id = NULL;

__attribute__((weak)) const char* spotflow_override_device_id()
{
	return NULL;
}

static void spotflow_generate_device_id();

const char* spotflow_get_device_id()
{
	if (cached_device_id != NULL) {
		return cached_device_id;
	}

	const char* device_id = spotflow_override_device_id();

	if (device_id == NULL) {
		if (strlen(device_id) > 0) {
			device_id = CONFIG_SPOTFLOW_DEVICE_ID;
		} else {
			spotflow_generate_device_id();
			device_id = spotflow_generated_device_id_buffer;
		}
	}

	cached_device_id = device_id;

	LOG_INF("Using Spotflow device ID: %s", cached_device_id);

	return cached_device_id;
}

static void spotflow_generate_device_id()
{
	uint8_t device_id[ZEPHYR_DEVICE_ID_MAX_LENGTH];

	ssize_t ret = hwinfo_get_device_id(device_id, ARRAY_SIZE(device_id));

	if (ret <= 0) {
		LOG_ERR("Failed to get Zephyr device ID (%d), using default", (int)ret);

		strncpy(spotflow_generated_device_id_buffer, "default_device_id",
			sizeof(spotflow_generated_device_id_buffer));
		return;
	}

	/* Convert the Zephyr device ID to its hexadecimal representation */
	for (int i = 0; i < ret; i++) {
		/* Each byte is converted to two characters and a null terminator.
		 * Each null terminator apart from the last one is overwritten by the
		 * first character of the next byte.
		 */
		snprintk(spotflow_generated_device_id_buffer + (2 * i), 3, "%02X", device_id[i]);
	}
}
