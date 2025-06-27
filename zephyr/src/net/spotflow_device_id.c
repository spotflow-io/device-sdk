#include <zephyr/drivers/hwinfo.h>

#include "spotflow_device_id.h"

#define ZEPHYR_DEVICE_ID_MAX_LENGTH 16

/* Hexadecimal representation of the device ID and a null terminator */
char spotflow_generated_device_id_buffer[(2 * ZEPHYR_DEVICE_ID_MAX_LENGTH) + 1];

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
	char* device_id = spotflow_override_device_id();

	if (device_id == NULL) {
		device_id = CONFIG_SPOTFLOW_DEVICE_ID;
	}

	if (strlen(device_id) == 0) {
		spotflow_generate_device_id();
		device_id = spotflow_generated_device_id_buffer;
	}

	return device_id;
}
