#ifndef SPOTFLOW_OTA_TEST_FAKES_H
#define SPOTFLOW_OTA_TEST_FAKES_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include <spotflow/ota.h>

#ifdef __cplusplus
extern "C" {
#endif

struct spotflow_ota_test_fake_mqtt {
	uint32_t publish_count;
	const uint8_t* last_payload;
	size_t last_payload_len;
	int publish_result;
};

struct spotflow_ota_test_fake_callbacks {
	enum spotflow_ota_result next_handle_result;
	bool block_handle;
	bool canceled_visible_during_handle;
	bool canceled_visible_during_notification;
	uint32_t handle_call_count;
	uint32_t cancel_call_count;
	k_tid_t handle_thread;
	k_tid_t cancel_thread;
	uint64_t last_attempt_id;
	bool last_is_main;
	char last_slug[33];
	char last_url[129];
	char last_secret[25];
	char last_version[65];
	struct k_sem handle_called_sem;
	struct k_sem handle_continue_sem;
	struct k_sem cancel_called_sem;
};

void spotflow_ota_test_fakes_reset(void);

struct spotflow_ota_test_fake_mqtt* spotflow_ota_test_fake_mqtt_get(void);

struct spotflow_ota_test_fake_callbacks* spotflow_ota_test_fake_callbacks_get(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_TEST_FAKES_H */
