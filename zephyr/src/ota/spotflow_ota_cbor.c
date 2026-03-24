#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#include <zephyr/logging/log.h>

#include <zcbor_common.h>
#include <zcbor_decode.h>

#include "ota/spotflow_ota_cbor.h"

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

#define KEY_MESSAGE_TYPE 0x00
#define KEY_IMAGE_URL 0x20

#define UPDATE_FIRMWARE_MESSAGE_TYPE 0x06

#define ZCBOR_STATE_DEPTH 1

int spotflow_ota_cbor_decode_update_firmware(uint8_t* payload, size_t len,
					     struct spotflow_ota_update_firmware_msg* msg)
{
	if (payload == NULL || len == 0) {
		LOG_ERR("Invalid payload or length");
		return -EINVAL;
	}

	*msg = (struct spotflow_ota_update_firmware_msg){ 0 };

	ZCBOR_STATE_D(state, ZCBOR_STATE_DEPTH, payload, len, 1, 0);

	bool success = zcbor_map_start_decode(state);

	success = success && zcbor_uint32_expect(state, KEY_MESSAGE_TYPE);
	success = success && zcbor_uint32_expect(state, UPDATE_FIRMWARE_MESSAGE_TYPE);

	success = success && zcbor_uint32_expect(state, KEY_IMAGE_URL);

	struct zcbor_string image_url;
	success = success && zcbor_tstr_decode(state, &image_url);

	if (!success) {
		LOG_ERR("Failed to decode OTA update firmware message: %d",
			zcbor_peek_error(state));
		return -EINVAL;
	}

	if (image_url.len > SPOTFLOW_OTA_IMAGE_URL_MAX_LENGTH) {
		LOG_ERR("Image URL length %zu exceeds maximum of %d bytes", image_url.len,
			SPOTFLOW_OTA_IMAGE_URL_MAX_LENGTH);
		return -EINVAL;
	}

	memcpy(msg->image_url, image_url.value, image_url.len);
	msg->image_url[image_url.len] = '\0';

	return 0;
}
