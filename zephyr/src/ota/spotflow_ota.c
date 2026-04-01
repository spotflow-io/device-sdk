#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>

#include "ota/spotflow_ota_download.h"
#include "ota/spotflow_ota.h"
#include "ota/spotflow_ota_cbor.h"
#include "net/spotflow_mqtt.h"

LOG_MODULE_REGISTER(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static char s_image_url[SPOTFLOW_OTA_IMAGE_URL_MAX_LENGTH + 1];
static K_SEM_DEFINE(s_download_sem, 0, 1);

static void handle_update_firmware_msg(uint8_t* payload, size_t len);

static void ota_download_thread_entry(void* p1, void* p2, void* p3);

K_THREAD_DEFINE(spotflow_ota_thread, CONFIG_SPOTFLOW_OTA_DOWNLOAD_THREAD_STACK_SIZE,
		ota_download_thread_entry, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0,
		0);

int spotflow_ota_init_session(void)
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

	strncpy(s_image_url, msg.image_url, sizeof(s_image_url) - 1);
	s_image_url[sizeof(s_image_url) - 1] = '\0';

	k_sem_give(&s_download_sem);
}

static void ota_download_thread_entry(void* p1, void* p2, void* p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		k_sem_take(&s_download_sem, K_FOREVER);

		LOG_INF("OTA download triggered for: %s", s_image_url);

		int rc = spotflow_ota_download_and_flash(s_image_url);

		if (rc < 0) {
			LOG_ERR("OTA firmware download failed: %d -- will retry on next update "
				"message",
				rc);
			continue;
		}

		rc = boot_request_upgrade(BOOT_UPGRADE_TEST);
		if (rc < 0) {
			LOG_ERR("boot_request_upgrade failed: %d", rc);
			continue;
		}

		LOG_INF("OTA image written and marked for test boot -- rebooting");
		sys_reboot(SYS_REBOOT_COLD);
	}
}
