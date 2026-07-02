#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "ota/protocol/spotflow_ota_cbor.h"
#include "ota/persistence/spotflow_ota_persistence.h"

#include "spotflow_ota_test_fakes.h"
#include "spotflow_ota_test_wait.h"

void spotflow_ota_test_wait_for_persisted_attempt(uint64_t attempt_id,
						  const enum spotflow_ota_result* expected_results,
						  size_t artifact_count)
{
	for (int i = 0; i < 100; i++) {
		struct spotflow_ota_persisted_attempt attempt;
		bool has_attempt;

		zassert_ok(spotflow_ota_persistence_load_attempt(&attempt, &has_attempt));
		if (has_attempt && attempt.attempt_id == attempt_id &&
		    attempt.artifact_count == artifact_count && !attempt.has_attempt_error &&
		    memcmp(attempt.artifact_results, expected_results,
			   artifact_count * sizeof(expected_results[0])) == 0) {
			return;
		}

		k_sleep(K_MSEC(10));
	}

	zassert_unreachable("Timed out waiting for persisted OTA attempt");
}

void spotflow_ota_test_wait_for_persisted_attempt_error(
    uint64_t attempt_id, enum spotflow_ota_attempt_error attempt_error)
{
	for (int i = 0; i < 100; i++) {
		struct spotflow_ota_persisted_attempt attempt;
		bool has_attempt;

		zassert_ok(spotflow_ota_persistence_load_attempt(&attempt, &has_attempt));
		if (has_attempt && attempt.attempt_id == attempt_id && attempt.has_attempt_error &&
		    attempt.attempt_error == attempt_error) {
			return;
		}

		k_sleep(K_MSEC(10));
	}

	zassert_unreachable("Timed out waiting for persisted OTA attempt error");
}

void spotflow_ota_test_expect_update_results_payload(
    const struct spotflow_ota_cbor_update_results* expected_message)
{
	struct spotflow_ota_test_fake_mqtt* fake_mqtt = spotflow_ota_test_fake_mqtt_get();
	uint8_t expected_payload[128];
	size_t expected_len;

	zassert_ok(spotflow_ota_cbor_encode_update_results(
	    expected_message, expected_payload, sizeof(expected_payload), &expected_len));
	zassert_equal(fake_mqtt->last_payload_len, expected_len);
	zassert_mem_equal(fake_mqtt->last_payload, expected_payload, expected_len);
}
