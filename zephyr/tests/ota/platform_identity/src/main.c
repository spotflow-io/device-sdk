#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "spotflow_build_id.h"
#include "ota/spotflow_ota_identity.h"
#include "ota/spotflow_ota_platform.h"
#include "spotflow_ota_bindesc_test_util.h"
#include "spotflow_ota_platform_fake.h"

static struct spotflow_ota_platform_fake* fake;

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);

	spotflow_ota_platform_fake_reset(spotflow_ota_platform_fake_get());
	fake = spotflow_ota_platform_fake_get();
}

static void write_bindesc_build_id(size_t offset, const uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	size_t end = spotflow_ota_test_bindesc_write_build_id(
	    fake->upload_slot, sizeof(fake->upload_slot), offset, build_id);

	zassert_true(end > 0);
	fake->upload_image_start = 0;
	fake->upload_image_size = end;
}

ZTEST(spotflow_ota_platform_identity, test_platform_fake_records_upgrade_confirm_reboot)
{
	zassert_ok(spotflow_ota_platform_request_test_upgrade());
	zassert_ok(spotflow_ota_platform_confirm_image());
	spotflow_ota_platform_reboot();

	zassert_equal(fake->upgrade_request_count, 1);
	zassert_equal(fake->confirm_count, 1);
	zassert_equal(fake->reboot_count, 1);
	zassert_true(spotflow_ota_platform_is_image_confirmed());
}

ZTEST(spotflow_ota_platform_identity, test_platform_fake_propagates_errors)
{
	fake->upgrade_request_result = -EIO;
	fake->confirm_result = -EIO;

	zassert_equal(spotflow_ota_platform_request_test_upgrade(), -EIO);
	zassert_equal(spotflow_ota_platform_confirm_image(), -EIO);
	zassert_false(spotflow_ota_platform_is_image_confirmed());
}

ZTEST(spotflow_ota_platform_identity, test_identity_compare_unavailable_when_running_missing)
{
	uint8_t expected[SPOTFLOW_BUILD_ID_LENGTH] = { 0x01 };

	zassert_equal(spotflow_ota_identity_compare_probation(expected),
		      SPOTFLOW_OTA_IDENTITY_UNAVAILABLE);
}

ZTEST(spotflow_ota_platform_identity, test_identity_compare_match_and_mismatch)
{
	uint8_t running[SPOTFLOW_BUILD_ID_LENGTH];
	uint8_t expected[SPOTFLOW_BUILD_ID_LENGTH];
	int rc = spotflow_ota_identity_get_running_build_id(running);

	if (rc == -ENOSYS) {
		ztest_test_skip();
	}

	zassert_ok(rc);
	memcpy(expected, running, sizeof(expected));

	zassert_equal(spotflow_ota_identity_compare_probation(expected),
		      SPOTFLOW_OTA_IDENTITY_MATCH);

	expected[0] ^= 0xff;
	zassert_equal(spotflow_ota_identity_compare_probation(expected),
		      SPOTFLOW_OTA_IDENTITY_MISMATCH);
}

ZTEST(spotflow_ota_platform_identity, test_identity_reads_downloaded_build_id)
{
	uint8_t expected[SPOTFLOW_BUILD_ID_LENGTH];
	uint8_t downloaded[SPOTFLOW_BUILD_ID_LENGTH];

	for (size_t i = 0; i < SPOTFLOW_BUILD_ID_LENGTH; i++) {
		expected[i] = (uint8_t)(0xA0 + i);
	}

	write_bindesc_build_id(32, expected);

	zassert_ok(spotflow_ota_identity_get_downloaded_build_id(downloaded));
	zassert_mem_equal(expected, downloaded, SPOTFLOW_BUILD_ID_LENGTH);
}

ZTEST(spotflow_ota_platform_identity, test_identity_reads_downloaded_build_id_across_chunk_boundary)
{
	uint8_t expected[SPOTFLOW_BUILD_ID_LENGTH];
	uint8_t downloaded[SPOTFLOW_BUILD_ID_LENGTH];

	for (size_t i = 0; i < SPOTFLOW_BUILD_ID_LENGTH; i++) {
		expected[i] = (uint8_t)(0xB0 + i);
	}

	write_bindesc_build_id(252, expected);

	zassert_ok(spotflow_ota_identity_get_downloaded_build_id(downloaded));
	zassert_mem_equal(expected, downloaded, SPOTFLOW_BUILD_ID_LENGTH);
}

ZTEST(spotflow_ota_platform_identity, test_downloaded_build_id_read_failure)
{
	fake->image_info_result = -EIO;

	uint8_t downloaded[SPOTFLOW_BUILD_ID_LENGTH];

	zassert_equal(spotflow_ota_identity_get_downloaded_build_id(downloaded), -EIO);
}

ZTEST(spotflow_ota_platform_identity, test_downloaded_build_id_missing_bindesc)
{
	fake->upload_image_start = 0;
	fake->upload_image_size = 64;

	uint8_t downloaded[SPOTFLOW_BUILD_ID_LENGTH];

	zassert_equal(spotflow_ota_identity_get_downloaded_build_id(downloaded), -ENOENT);
}

ZTEST_SUITE(spotflow_ota_platform_identity, NULL, NULL, before_each, NULL, NULL);
