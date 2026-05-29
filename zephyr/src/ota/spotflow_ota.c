#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>

#include "ota/spotflow_ota.h"
#include "ota/spotflow_ota_cbor.h"
#include "ota/spotflow_ota_download.h"
#include "net/spotflow_mqtt.h"

LOG_MODULE_REGISTER(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static char image_url[SPOTFLOW_OTA_ARTIFACT_URL_MAX_LENGTH + 1];
static char image_secret[SPOTFLOW_OTA_ARTIFACT_SECRET_MAX_LENGTH + 1];
static K_MUTEX_DEFINE(image_url_mutex);
static K_SEM_DEFINE(download_sem, 0, 1);

static void handle_ota_c2d_msg(uint8_t* payload, size_t len);

static void ota_download_thread_entry(void* p1, void* p2, void* p3);

K_THREAD_DEFINE(spotflow_ota_thread, CONFIG_SPOTFLOW_OTA_DOWNLOAD_THREAD_STACK_SIZE,
		ota_download_thread_entry, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0,
		0);

int spotflow_ota_init_session(void)
{
	int rc = spotflow_mqtt_request_ota_subscription(handle_ota_c2d_msg);
	if (rc < 0) {
		LOG_ERR("Failed to request subscription to OTA topic: %d", rc);
		return rc;
	}

	return 0;
}

static void handle_ota_c2d_msg(uint8_t* payload, size_t len)
{
	struct spotflow_ota_cbor_c2d_msg msg;
	struct spotflow_ota_cbor_decode_status status;
	int rc = spotflow_ota_cbor_decode_c2d(payload, len, &msg, &status);

	if (rc < 0) {
		if (status.has_trustworthy_attempt_id && status.has_attempt_error) {
			LOG_ERR("Rejected OTA attempt %llu with attempt error %d",
				status.attempt_id, status.attempt_error);
		} else {
			LOG_ERR("Failed to decode received OTA message: %d", rc);
		}

		return;
	}

	if (msg.type != SPOTFLOW_OTA_CBOR_MSG_UPDATE_ARTIFACTS) {
		LOG_DBG("Ignoring unsupported OTA C2D message type in prototype handler: %u",
			msg.type);
		return;
	}

	if (msg.payload.update.artifact_count == 0) {
		LOG_ERR("Accepted OTA update attempt without artifacts");
		return;
	}

	const struct spotflow_ota_artifact* artifact = &msg.payload.update.artifacts[0];

	for (size_t i = 0; i < msg.payload.update.artifact_count; i++) {
		if (msg.payload.update.artifacts[i].is_main) {
			artifact = &msg.payload.update.artifacts[i];
			break;
		}
	}

	LOG_INF("OTA firmware update requested: slug=%s version=%s", artifact->slug,
		artifact->version);

	k_mutex_lock(&image_url_mutex, K_FOREVER);
	strncpy(image_url, artifact->url, sizeof(image_url) - 1);
	image_url[sizeof(image_url) - 1] = '\0';
	strncpy(image_secret, artifact->secret, sizeof(image_secret) - 1);
	image_secret[sizeof(image_secret) - 1] = '\0';
	k_mutex_unlock(&image_url_mutex);

	k_sem_give(&download_sem);
}

static void ota_download_thread_entry(void* p1, void* p2, void* p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		char image_url_copy[sizeof(image_url)];
		char image_secret_copy[sizeof(image_secret)];

		k_sem_take(&download_sem, K_FOREVER);

		k_mutex_lock(&image_url_mutex, K_FOREVER);
		strncpy(image_url_copy, image_url, sizeof(image_url_copy));
		image_url_copy[sizeof(image_url_copy) - 1] = '\0';
		strncpy(image_secret_copy, image_secret, sizeof(image_secret_copy));
		image_secret_copy[sizeof(image_secret_copy) - 1] = '\0';
		k_mutex_unlock(&image_url_mutex);

		LOG_INF("OTA download triggered");

		int rc = spotflow_ota_download_and_flash(image_url_copy, image_secret_copy);

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
