#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "spotflow_build_id.h"
#include "ota/persistence/spotflow_ota_records_cbor.h"

LOG_MODULE_REGISTER(spotflow_ota);

static void expect_attempt_equal(const struct spotflow_ota_persisted_attempt* actual,
				 const struct spotflow_ota_persisted_attempt* expected)
{
	zassert_equal(actual->attempt_id, expected->attempt_id);
	zassert_equal(actual->has_attempt_error, expected->has_attempt_error);
	zassert_equal(actual->attempt_error, expected->attempt_error);
	zassert_equal(actual->artifact_count, expected->artifact_count);
	zassert_equal(actual->actionable_cancellation, expected->actionable_cancellation);
	for (size_t i = 0; i < expected->artifact_count; i++) {
		zassert_equal(actual->artifact_results[i], expected->artifact_results[i]);
	}
}

static void expect_probation_equal(const struct spotflow_ota_probation* actual,
				   const struct spotflow_ota_probation* expected)
{
	zassert_equal(actual->attempt_id, expected->attempt_id);
	zassert_equal(actual->artifact_index, expected->artifact_index);
	zassert_str_equal(actual->slug, expected->slug);
	zassert_str_equal(actual->version, expected->version);
	zassert_mem_equal(actual->expected_build_id, expected->expected_build_id,
			  SPOTFLOW_BUILD_ID_LENGTH);
}

ZTEST(spotflow_ota_records_cbor, test_round_trip_attempt_record)
{
	struct spotflow_ota_persisted_attempt input = {
		.attempt_id = 101,
		.artifact_count = 3,
		.actionable_cancellation = true,
		.artifact_results = {
			SPOTFLOW_OTA_RESULT_SUCCEEDED,
			SPOTFLOW_OTA_RESULT_FAILED,
			SPOTFLOW_OTA_RESULT_CANCELED,
		},
	};
	uint8_t buffer[128];
	size_t encoded_len;
	struct spotflow_ota_persisted_attempt decoded;

	zassert_ok(
	    spotflow_ota_records_cbor_encode_attempt(&input, buffer, sizeof(buffer), &encoded_len));
	zassert_ok(spotflow_ota_records_cbor_decode_attempt(buffer, encoded_len, &decoded));

	expect_attempt_equal(&decoded, &input);
}

ZTEST(spotflow_ota_records_cbor, test_round_trip_attempt_error_record)
{
	struct spotflow_ota_persisted_attempt input = {
		.attempt_id = 102,
		.has_attempt_error = true,
		.attempt_error = SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE,
	};
	uint8_t buffer[64];
	size_t encoded_len;
	struct spotflow_ota_persisted_attempt decoded;

	zassert_ok(
	    spotflow_ota_records_cbor_encode_attempt(&input, buffer, sizeof(buffer), &encoded_len));
	zassert_ok(spotflow_ota_records_cbor_decode_attempt(buffer, encoded_len, &decoded));

	expect_attempt_equal(&decoded, &input);
}

ZTEST(spotflow_ota_records_cbor, test_round_trip_probation_record)
{
	struct spotflow_ota_probation input = {
		.attempt_id = 7,
		.artifact_index = 1,
		.slug = "main",
		.version = "1.2.3",
	};
	for (size_t i = 0; i < SPOTFLOW_BUILD_ID_LENGTH; i++) {
		input.expected_build_id[i] = (uint8_t)(i + 1);
	}

	uint8_t buffer[128];
	size_t encoded_len;
	struct spotflow_ota_probation decoded;

	zassert_ok(spotflow_ota_records_cbor_encode_probation(&input, buffer, sizeof(buffer),
							      &encoded_len));
	zassert_ok(spotflow_ota_records_cbor_decode_probation(buffer, encoded_len, &decoded));

	expect_probation_equal(&decoded, &input);
}

ZTEST(spotflow_ota_records_cbor, test_reject_unsupported_attempt_schema)
{
	static const uint8_t payload[] = {
		0xa5, 0x00, 0x02, 0x01, 0x01, 0x03, 0x01, 0x04, 0xf4, 0x05, 0x81, 0x00,
	};
	struct spotflow_ota_persisted_attempt decoded;

	zassert_true(spotflow_ota_records_cbor_decode_attempt(payload, sizeof(payload), &decoded) <
		     0);
}

ZTEST(spotflow_ota_records_cbor, test_reject_truncated_probation_record)
{
	struct spotflow_ota_probation input = {
		.attempt_id = 8,
		.artifact_index = 0,
		.slug = "main",
		.version = "2.0.0",
	};
	for (size_t i = 0; i < SPOTFLOW_BUILD_ID_LENGTH; i++) {
		input.expected_build_id[i] = (uint8_t)(0xa0 + i);
	}

	uint8_t buffer[128];
	size_t encoded_len;
	struct spotflow_ota_probation decoded;

	zassert_ok(spotflow_ota_records_cbor_encode_probation(&input, buffer, sizeof(buffer),
							      &encoded_len));
	zassert_true(spotflow_ota_records_cbor_decode_probation(buffer, encoded_len - 1, &decoded) <
		     0);
}

ZTEST_SUITE(spotflow_ota_records_cbor, NULL, NULL, NULL, NULL, NULL);
