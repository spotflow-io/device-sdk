#include "ota/platform/spotflow_ota_identity_stream.h"

#include <errno.h>
#include <string.h>

#include <zephyr/bindesc.h>
#include <zephyr/ztest.h>

#define DESCRIPTOR_OFFSET 12
#define ID_OFFSET (DESCRIPTOR_OFFSET + sizeof(uint64_t) + 16)

static size_t make_descriptors(uint8_t* image, uint16_t id_size);

ZTEST(spotflow_ota_identity_stream, test_every_block_boundary)
{
	uint8_t image[80] = { 0 };
	size_t length = make_descriptors(image, SPOTFLOW_BUILD_ID_LENGTH);

	for (size_t split = 0; split <= length; split++) {
		struct spotflow_ota_identity_stream stream = { 0 };
		uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];

		spotflow_ota_identity_stream_feed(&stream, image, split);
		spotflow_ota_identity_stream_feed(&stream, image + split, length - split);
		zassert_ok(spotflow_ota_identity_stream_get(&stream, build_id));
		zassert_mem_equal(build_id, image + ID_OFFSET, sizeof(build_id));
	}
}

ZTEST(spotflow_ota_identity_stream, test_single_bytes_and_zero_length_blocks)
{
	struct spotflow_ota_identity_stream stream = { 0 };
	uint8_t image[80] = { 0 };
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];
	size_t length = make_descriptors(image, SPOTFLOW_BUILD_ID_LENGTH);

	for (size_t i = 0; i < length; i++) {
		spotflow_ota_identity_stream_feed(&stream, NULL, 0);
		spotflow_ota_identity_stream_feed(&stream, image + i, 1);
	}
	zassert_ok(spotflow_ota_identity_stream_get(&stream, build_id));
	zassert_mem_equal(build_id, image + ID_OFFSET, sizeof(build_id));
}

ZTEST(spotflow_ota_identity_stream, test_truncation_at_every_byte)
{
	uint8_t image[80] = { 0 };
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];

	make_descriptors(image, SPOTFLOW_BUILD_ID_LENGTH);
	for (size_t length = 0; length < ID_OFFSET + SPOTFLOW_BUILD_ID_LENGTH; length++) {
		struct spotflow_ota_identity_stream stream = { 0 };

		spotflow_ota_identity_stream_feed(&stream, image, length);
		zassert_equal(spotflow_ota_identity_stream_get(&stream, build_id), -ENOENT);
	}
}

ZTEST(spotflow_ota_identity_stream, test_invalid_size_and_unpatched_id)
{
	struct spotflow_ota_identity_stream stream = { 0 };
	uint8_t image[80] = { 0 };
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];
	size_t length = make_descriptors(image, SPOTFLOW_BUILD_ID_LENGTH - 1);

	spotflow_ota_identity_stream_feed(&stream, image, length);
	zassert_equal(spotflow_ota_identity_stream_get(&stream, build_id), -EINVAL);

	memset(&stream, 0, sizeof(stream));
	length = make_descriptors(image, SPOTFLOW_BUILD_ID_LENGTH);
	memset(image + ID_OFFSET, 0, SPOTFLOW_BUILD_ID_LENGTH);
	spotflow_ota_identity_stream_feed(&stream, image, length);
	zassert_equal(spotflow_ota_identity_stream_get(&stream, build_id), -ENOSYS);
}

ZTEST(spotflow_ota_identity_stream, test_end_marker_and_unaligned_magic)
{
	struct spotflow_ota_identity_stream stream = { 0 };
	uint8_t image[80] = { 0 };
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];
	size_t length = make_descriptors(image, SPOTFLOW_BUILD_ID_LENGTH);
	uint16_t end_tag = BINDESC_TAG_DESCRIPTORS_END;

	memcpy(image + ID_OFFSET - 4, &end_tag, sizeof(end_tag));
	spotflow_ota_identity_stream_feed(&stream, image, length);
	zassert_equal(spotflow_ota_identity_stream_get(&stream, build_id), -ENOENT);

	memset(&stream, 0, sizeof(stream));
	make_descriptors(image, SPOTFLOW_BUILD_ID_LENGTH);
	memmove(image + 1, image, length);
	spotflow_ota_identity_stream_feed(&stream, image, length + 1);
	zassert_equal(spotflow_ota_identity_stream_get(&stream, build_id), -ENOENT);
}

ZTEST(spotflow_ota_identity_stream, test_null_arguments)
{
	struct spotflow_ota_identity_stream stream = { 0 };
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];

	zassert_equal(spotflow_ota_identity_stream_get(NULL, build_id), -EINVAL);
	zassert_equal(spotflow_ota_identity_stream_get(&stream, NULL), -EINVAL);
	spotflow_ota_identity_stream_feed(&stream, NULL, 1);
	zassert_equal(spotflow_ota_identity_stream_get(&stream, build_id), -EINVAL);
}

static size_t make_descriptors(uint8_t* image, uint16_t id_size)
{
	uint64_t magic = BINDESC_MAGIC;
	/* Empty and padded unknown descriptors must both be skipped. */
	uint16_t entries[] = {
		BINDESC_TAG(BYTES, 1),	   0,	   BINDESC_TAG(BYTES, 2), 3, 0x1234, 0,
		BINDESC_TAG(BYTES, 0x5f0), id_size
	};

	memcpy(image + DESCRIPTOR_OFFSET, &magic, sizeof(magic));
	memcpy(image + DESCRIPTOR_OFFSET + sizeof(magic), entries, sizeof(entries));
	for (size_t i = 0; i < SPOTFLOW_BUILD_ID_LENGTH; i++) {
		image[ID_OFFSET + i] = i + 1;
	}
	return ID_OFFSET + SPOTFLOW_BUILD_ID_LENGTH;
}

ZTEST_SUITE(spotflow_ota_identity_stream, NULL, NULL, NULL, NULL, NULL);
