#include "ota/platform/mcuboot/spotflow_ota_mcuboot_stream.h"
#include "spotflow_ota_bindesc_test_util.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#define PAYLOAD_START 96
#define PAYLOAD_SIZE 64

static uint8_t image[224];
static uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];

static void before_each(void* fixture);
static int feed_image(struct spotflow_ota_mcuboot_stream* stream, size_t split, size_t length);

ZTEST(spotflow_ota_mcuboot_stream, test_every_split_skips_header_padding_and_trailer)
{
	uint8_t actual[SPOTFLOW_BUILD_ID_LENGTH];

	for (size_t split = 0; split <= sizeof(image); split++) {
		struct spotflow_ota_mcuboot_stream stream = { 0 };

		zassert_ok(feed_image(&stream, split, sizeof(image)));
		zassert_ok(spotflow_ota_mcuboot_stream_get_build_id(&stream, actual));
		zassert_mem_equal(actual, build_id, sizeof(actual));
	}
}

ZTEST(spotflow_ota_mcuboot_stream, test_single_byte_blocks)
{
	struct spotflow_ota_mcuboot_stream stream = { 0 };
	uint8_t actual[SPOTFLOW_BUILD_ID_LENGTH];

	for (size_t i = 0; i < sizeof(image); i++) {
		struct spotflow_artifact_block block = {
			.data = image + i,
			.data_len = 1,
			.offset = i,
			.is_last = i == sizeof(image) - 1,
		};

		zassert_ok(spotflow_ota_mcuboot_stream_feed(&stream, &block, sizeof(image)));
	}
	zassert_ok(spotflow_ota_mcuboot_stream_get_build_id(&stream, actual));
	zassert_mem_equal(actual, build_id, sizeof(actual));
}

ZTEST(spotflow_ota_mcuboot_stream, test_descriptors_outside_payload_cannot_supply_identity)
{
	struct spotflow_ota_mcuboot_stream stream = { 0 };
	uint8_t actual[SPOTFLOW_BUILD_ID_LENGTH];

	memset(image + PAYLOAD_START, 0xff, PAYLOAD_SIZE);
	zassert_ok(feed_image(&stream, 1, sizeof(image)));
	zassert_equal(spotflow_ota_mcuboot_stream_get_build_id(&stream, actual), -ENOENT);
}

ZTEST(spotflow_ota_mcuboot_stream, test_rejects_bad_magic_size_and_encoding)
{
	struct spotflow_ota_mcuboot_stream stream = { 0 };

	image[0] = 0;
	zassert_equal(feed_image(&stream, 1, sizeof(image)), -EBADMSG);
	memset(&stream, 0, sizeof(stream));
	sys_put_le32(0x96f3b83d, image);
	sys_put_le32(UINT32_MAX, image + 12);
	zassert_equal(feed_image(&stream, 1, sizeof(image)), -EBADMSG);
	memset(&stream, 0, sizeof(stream));
	sys_put_le32(PAYLOAD_SIZE, image + 12);
	sys_put_le32(BIT(2), image + 16);
	zassert_equal(feed_image(&stream, 1, sizeof(image)), -ENOTSUP);
}

ZTEST(spotflow_ota_mcuboot_stream, test_truncated_payload_cannot_supply_identity)
{
	struct spotflow_ota_mcuboot_stream stream = { 0 };
	uint8_t actual[SPOTFLOW_BUILD_ID_LENGTH];

	zassert_equal(feed_image(&stream, 1, PAYLOAD_START + PAYLOAD_SIZE - 1), -EBADMSG);
	zassert_equal(spotflow_ota_mcuboot_stream_get_build_id(&stream, actual), -EBADMSG);
}

ZTEST(spotflow_ota_mcuboot_stream, test_rejects_wrong_offset_and_storage_overflow)
{
	struct spotflow_ota_mcuboot_stream stream = { 0 };
	struct spotflow_artifact_block block = {
		.data = image,
		.data_len = sizeof(image),
		.offset = 1,
	};

	zassert_equal(spotflow_ota_mcuboot_stream_feed(&stream, &block, sizeof(image)), -EINVAL);
	block.offset = 0;
	zassert_equal(spotflow_ota_mcuboot_stream_feed(&stream, &block, sizeof(image) - 1), -EFBIG);
}

static int feed_image(struct spotflow_ota_mcuboot_stream* stream, size_t split, size_t length)
{
	struct spotflow_artifact_block block = {
		.data = image,
		.data_len = split,
	};
	int rc = spotflow_ota_mcuboot_stream_feed(stream, &block, sizeof(image));

	if (rc < 0) {
		return rc;
	}
	block.data += split;
	block.offset = split;
	block.data_len = length - split;
	block.is_last = true;
	return spotflow_ota_mcuboot_stream_feed(stream, &block, sizeof(image));
}

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);
	memset(image, 0, sizeof(image));
	memset(build_id, 0x55, sizeof(build_id));
	sys_put_le32(0x96f3b83d, image);
	sys_put_le16(PAYLOAD_START, image + 8);
	sys_put_le32(PAYLOAD_SIZE, image + 12);
	uint8_t other_id[SPOTFLOW_BUILD_ID_LENGTH] = { 0x99 };

	zassert_true(spotflow_ota_test_bindesc_write_build_id(image, sizeof(image), 40, other_id));
	zassert_true(spotflow_ota_test_bindesc_write_build_id(image, sizeof(image), PAYLOAD_START,
							      build_id));
	zassert_true(spotflow_ota_test_bindesc_write_build_id(
		image, sizeof(image), PAYLOAD_START + PAYLOAD_SIZE, other_id));
}

ZTEST_SUITE(spotflow_ota_mcuboot_stream, NULL, NULL, before_each, NULL, NULL);
