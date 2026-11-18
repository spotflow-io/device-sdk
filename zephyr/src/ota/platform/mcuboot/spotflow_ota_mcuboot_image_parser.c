#include "spotflow_ota_mcuboot_image_parser.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

/* MCUboot v1 wire header, also decoded by Zephyr's boot_read_bank_header().
 * The public Zephyr API exposes decoded metadata but no streaming decoder.
 * Container fields are little-endian, independently of the executable's endian.
 */
#define MCUBOOT_IMAGE_MAGIC 0x96f3b83dU
#define MCUBOOT_HEADER_SIZE_OFFSET 8U
#define MCUBOOT_IMAGE_SIZE_OFFSET 12U
#define MCUBOOT_FLAGS_OFFSET 16U
#define MCUBOOT_SUPPORTED_FLAGS (BIT(5) | BIT(8)) /* RAM_LOAD and ROM_FIXED */

static int parse_header(struct spotflow_ota_mcuboot_image_parser* parser);

int spotflow_ota_mcuboot_image_parser_init(struct spotflow_ota_mcuboot_image_parser* parser,
					   size_t max_size)
{
	if (parser == NULL || max_size == 0) {
		return -EINVAL;
	}
	memset(parser, 0, sizeof(*parser));
	parser->max_size = max_size;
	return 0;
}

int spotflow_ota_mcuboot_image_parser_feed(struct spotflow_ota_mcuboot_image_parser* parser,
					   const struct spotflow_artifact_block* block)
{
	if (parser == NULL || block == NULL || (block->data == NULL && block->data_len != 0) ||
	    parser->artifact_complete || block->offset != parser->artifact_offset) {
		return -EINVAL;
	}
	if (parser->artifact_offset > parser->max_size ||
	    block->data_len > parser->max_size - parser->artifact_offset) {
		return -EFBIG;
	}
	const uint8_t* data = block->data;
	size_t remaining = block->data_len;

	while (remaining > 0) {
		size_t length;

		if (parser->artifact_offset < sizeof(parser->header)) {
			length = MIN(remaining, sizeof(parser->header) - parser->artifact_offset);
			memcpy(parser->header + parser->artifact_offset, data, length);
			parser->artifact_offset += length;
			if (parser->artifact_offset == sizeof(parser->header)) {
				int rc = parse_header(parser);

				if (rc < 0) {
					return rc;
				}
			}
		} else {
			length = remaining;
			size_t start = MAX(parser->artifact_offset, parser->payload_start);
			size_t end = MIN(parser->artifact_offset + length, parser->payload_end);

			if (end > start) {
				spotflow_ota_build_id_parser_feed(
					&parser->build_id_parser,
					data + start - parser->artifact_offset, end - start);
			}
			parser->artifact_offset += length;
		}
		data += length;
		remaining -= length;
	}
	if (block->is_last) {
		if (parser->payload_end == 0 || parser->artifact_offset < parser->payload_end) {
			return -EBADMSG;
		}
		parser->artifact_complete = true;
	}
	return 0;
}

int spotflow_ota_mcuboot_image_parser_get_build_id(
	const struct spotflow_ota_mcuboot_image_parser* parser,
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	if (parser == NULL || build_id == NULL) {
		return -EINVAL;
	}
	if (!parser->artifact_complete) {
		return -EBADMSG;
	}
	return spotflow_ota_build_id_parser_get_build_id(&parser->build_id_parser, build_id);
}

static int parse_header(struct spotflow_ota_mcuboot_image_parser* parser)
{
	size_t header_size = sys_get_le16(parser->header + MCUBOOT_HEADER_SIZE_OFFSET);
	size_t image_size = sys_get_le32(parser->header + MCUBOOT_IMAGE_SIZE_OFFSET);
	uint32_t flags = sys_get_le32(parser->header + MCUBOOT_FLAGS_OFFSET);

	if (sys_get_le32(parser->header) != MCUBOOT_IMAGE_MAGIC ||
	    header_size < sizeof(parser->header) || header_size % 4 != 0 || image_size == 0 ||
	    header_size > parser->max_size || image_size > parser->max_size - header_size) {
		return -EBADMSG;
	}
	/* Encrypted, compressed and split images do not expose the running payload. */
	if ((flags & ~MCUBOOT_SUPPORTED_FLAGS) != 0) {
		return -ENOTSUP;
	}
	parser->payload_start = header_size;
	parser->payload_end = header_size + image_size;
	return 0;
}
