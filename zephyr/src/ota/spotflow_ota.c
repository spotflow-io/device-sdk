#include <stdint.h>

#include <zephyr/logging/log.h>

#include "ota/spotflow_ota.h"
#include "ota/spotflow_ota_cbor.h"
#include "net/spotflow_mqtt.h"

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static void handle_update_firmware_msg(uint8_t* payload, size_t len);

int spotflow_ota_init_session()
{
	int rc = spotflow_mqtt_request_ota_subscription(handle_update_firmware_msg);
	if (rc < 0) {
		LOG_ERR("Failed to request subscription to OTA topic: %d", rc);
		return rc;
	}

	return 0;
}

static void handle_update_firmware_msg(uint8_t* payload, size_t len)
{
	struct spotflow_ota_update_firmware_msg msg;
	int rc = spotflow_ota_cbor_decode_update_firmware(payload, len, &msg);
	if (rc < 0) {
		LOG_ERR("Failed to decode received OTA update firmware message: %d", rc);
		return;
	}

	/* TODO: Replace just by the version after the message includes it (the URL is a secret) */
	LOG_INF("OTA firmware update requested: %s", msg.image_url);
}
