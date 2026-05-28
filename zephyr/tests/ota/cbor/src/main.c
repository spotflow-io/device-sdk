#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "ota/spotflow_ota_cbor.h"

LOG_MODULE_REGISTER(spotflow_ota);

static const uint8_t update_artifacts_payload[] = {
	0xa3, 0x00, 0x06, 0x18, 0x20, 0x01, 0x18, 0x22, 0x81, 0xa6, 0x18, 0x23, 0x00,
	0x18, 0x24, 0x64, 0x6d, 0x61, 0x69, 0x6e, 0x18, 0x25, 0xf5, 0x18, 0x26, 0x69,
	0x68, 0x74, 0x74, 0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x61, 0x18, 0x27, 0x66, 0x73,
	0x65, 0x63, 0x72, 0x65, 0x74, 0x18, 0x28, 0x65, 0x31, 0x2e, 0x30, 0x2e, 0x30,
};

static const uint8_t cancel_update_payload[] = {
	0xa2, 0x00, 0x07, 0x18, 0x20, 0x02,
};

static const uint8_t report_update_results_payload[] = {
	0xa2, 0x00, 0x08, 0x18, 0x20, 0x03,
};

static const uint8_t update_results_payload[] = {
	0xa5, 0x00, 0x09, 0x18, 0x20, 0x07, 0x18, 0x2a, 0x81,
	0x00, 0x18, 0x2b, 0x81, 0x01, 0x18, 0x2c, 0x81, 0x02,
};

static const uint8_t update_results_error_payload[] = {
	0xa3, 0x00, 0x09, 0x18, 0x20, 0x09, 0x18, 0x2d, 0x03,
};

ZTEST(spotflow_ota_cbor, test_decode_update_artifacts)
{
	struct spotflow_ota_cbor_c2d_msg msg;
	struct spotflow_ota_cbor_decode_status status;

	int rc = spotflow_ota_cbor_decode_c2d(update_artifacts_payload,
					      sizeof(update_artifacts_payload), &msg, &status);

	zassert_ok(rc);
	zassert_true(status.has_trustworthy_attempt_id);
	zassert_equal(status.attempt_id, 1);
	zassert_equal(msg.type, SPOTFLOW_OTA_CBOR_MSG_UPDATE_ARTIFACTS);
	zassert_equal(msg.attempt_id, 1);
	zassert_false(msg.payload.update.is_canceled);
	zassert_equal(msg.payload.update.artifact_count, 1);

	const struct spotflow_ota_cbor_artifact* artifact = &msg.payload.update.artifacts[0];

	zassert_equal(artifact->type, SPOTFLOW_OTA_CBOR_ARTIFACT_TYPE_FIRMWARE);
	zassert_true(artifact->is_main);
	zassert_str_equal(artifact->slug, "main");
	zassert_str_equal(artifact->url, "https://a");
	zassert_str_equal(artifact->secret, "secret");
	zassert_str_equal(artifact->version, "1.0.0");
}

ZTEST(spotflow_ota_cbor, test_decode_cancel_update)
{
	struct spotflow_ota_cbor_c2d_msg msg;
	struct spotflow_ota_cbor_decode_status status;

	int rc = spotflow_ota_cbor_decode_c2d(cancel_update_payload, sizeof(cancel_update_payload),
					      &msg, &status);

	zassert_ok(rc);
	zassert_true(status.has_trustworthy_attempt_id);
	zassert_equal(msg.type, SPOTFLOW_OTA_CBOR_MSG_CANCEL_UPDATE);
	zassert_equal(msg.attempt_id, 2);
}

ZTEST(spotflow_ota_cbor, test_decode_report_update_results)
{
	struct spotflow_ota_cbor_c2d_msg msg;
	struct spotflow_ota_cbor_decode_status status;

	int rc = spotflow_ota_cbor_decode_c2d(report_update_results_payload,
					      sizeof(report_update_results_payload), &msg, &status);

	zassert_ok(rc);
	zassert_true(status.has_trustworthy_attempt_id);
	zassert_equal(msg.type, SPOTFLOW_OTA_CBOR_MSG_REPORT_UPDATE_RESULTS);
	zassert_equal(msg.attempt_id, 3);
}

ZTEST(spotflow_ota_cbor, test_encode_update_results)
{
	uint8_t buffer[32];
	size_t encoded_len;
	struct spotflow_ota_cbor_update_results msg = {
		.attempt_id = 7,
		.succeeded_count = 1,
		.succeeded = { 0 },
		.failed_count = 1,
		.failed = { 1 },
		.canceled_count = 1,
		.canceled = { 2 },
	};

	int rc =
	    spotflow_ota_cbor_encode_update_results(&msg, buffer, sizeof(buffer), &encoded_len);

	zassert_ok(rc);
	zassert_equal(encoded_len, sizeof(update_results_payload));
	zassert_mem_equal(buffer, update_results_payload, sizeof(update_results_payload));
}

ZTEST(spotflow_ota_cbor, test_encode_update_results_with_attempt_error)
{
	uint8_t buffer[16];
	size_t encoded_len;
	struct spotflow_ota_cbor_update_results msg = {
		.attempt_id = 9,
		.has_attempt_error = true,
		.attempt_error = SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE,
		.succeeded_count = 1,
		.succeeded = { 0 },
	};

	int rc =
	    spotflow_ota_cbor_encode_update_results(&msg, buffer, sizeof(buffer), &encoded_len);

	zassert_ok(rc);
	zassert_equal(encoded_len, sizeof(update_results_error_payload));
	zassert_mem_equal(buffer, update_results_error_payload,
			  sizeof(update_results_error_payload));
}

ZTEST(spotflow_ota_cbor, test_reject_unknown_artifact_type_with_attempt_error)
{
	uint8_t payload[sizeof(update_artifacts_payload)];
	struct spotflow_ota_cbor_c2d_msg msg;
	struct spotflow_ota_cbor_decode_status status;

	memcpy(payload, update_artifacts_payload, sizeof(payload));
	payload[12] = 0x01;

	int rc = spotflow_ota_cbor_decode_c2d(payload, sizeof(payload), &msg, &status);

	zassert_equal(rc, -EINVAL);
	zassert_true(status.has_trustworthy_attempt_id);
	zassert_equal(status.attempt_id, 1);
	zassert_equal(status.attempt_error, SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_UNKNOWN_ARTIFACT_TYPE);
}

ZTEST(spotflow_ota_cbor, test_reject_zero_attempt_id_without_trustworthy_attempt_id)
{
	uint8_t payload[sizeof(cancel_update_payload)];
	struct spotflow_ota_cbor_c2d_msg msg;
	struct spotflow_ota_cbor_decode_status status;

	memcpy(payload, cancel_update_payload, sizeof(payload));
	payload[5] = 0x00;

	int rc = spotflow_ota_cbor_decode_c2d(payload, sizeof(payload), &msg, &status);

	zassert_equal(rc, -EINVAL);
	zassert_false(status.has_trustworthy_attempt_id);
}

ZTEST_SUITE(spotflow_ota_cbor, NULL, NULL, NULL, NULL, NULL);
