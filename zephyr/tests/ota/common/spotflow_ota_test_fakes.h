#ifndef SPOTFLOW_OTA_TEST_FAKES_H
#define SPOTFLOW_OTA_TEST_FAKES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct spotflow_ota_test_fake_mqtt {
	uint32_t publish_count;
	const uint8_t *last_payload;
	size_t last_payload_len;
	int publish_result;
};

void spotflow_ota_test_fakes_reset(void);

struct spotflow_ota_test_fake_mqtt *spotflow_ota_test_fake_mqtt_get(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_TEST_FAKES_H */
