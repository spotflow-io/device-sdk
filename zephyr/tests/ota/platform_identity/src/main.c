#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "spotflow_build_id.h"
#include "ota/spotflow_ota_identity.h"
#include "ota/spotflow_ota_platform.h"
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
	static const uint8_t bindesc_magic[] = { 0x46, 0x60, 0xa4, 0x7e, 0x5a, 0x3e, 0x86, 0xb9 };
	uint8_t* p = fake->upload_slot + offset;

	memcpy(p, bindesc_magic, sizeof(bindesc_magic));
	p += sizeof(bindesc_magic);
	p[0] = 0xf0;
	p[1] = 0x25;
	p[2] = SPOTFLOW_BUILD_ID_LENGTH;
	p[3] = 0x00;
	memcpy(p + 4, build_id, SPOTFLOW_BUILD_ID_LENGTH);
	p += 4 + SPOTFLOW_BUILD_ID_LENGTH;
	p[0] = 0xff;
	p[1] = 0xff;
	p[2] = 0x00;
	p[3] = 0x00;

	fake->upload_image_start = 0;
	fake->upload_image_size = offset + sizeof(bindesc_magic) + 4 + SPOTFLOW_BUILD_ID_LENGTH + 4;
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
