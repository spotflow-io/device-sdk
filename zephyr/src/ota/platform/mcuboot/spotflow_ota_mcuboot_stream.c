#include "spotflow_ota_mcuboot_stream.h"

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

static int read_header(struct spotflow_ota_mcuboot_stream* stream, size_t max_size);

int spotflow_ota_mcuboot_stream_feed(struct spotflow_ota_mcuboot_stream* stream,
				     const struct spotflow_artifact_block* block, size_t max_size)
{
	if (stream == NULL || block == NULL || (block->data == NULL && block->data_len != 0) ||
	    stream->complete || block->offset != stream->offset) {
		return -EINVAL;
	}
	if (stream->offset > max_size || block->data_len > max_size - stream->offset) {
		return -EFBIG;
	}
	const uint8_t* data = block->data;
	size_t remaining = block->data_len;

	while (remaining > 0) {
		size_t length;

		if (stream->offset < sizeof(stream->header)) {
			length = MIN(remaining, sizeof(stream->header) - stream->offset);
			memcpy(stream->header + stream->offset, data, length);
			stream->offset += length;
			if (stream->offset == sizeof(stream->header)) {
				int rc = read_header(stream, max_size);

				if (rc < 0) {
					return rc;
				}
			}
		} else {
			length = remaining;
			size_t start = MAX(stream->offset, stream->payload_start);
			size_t end = MIN(stream->offset + length, stream->payload_end);

			if (end > start) {
				spotflow_ota_identity_stream_feed(&stream->identity,
								  data + start - stream->offset,
								  end - start);
			}
			stream->offset += length;
		}
		data += length;
		remaining -= length;
	}
	if (block->is_last) {
		if (stream->payload_end == 0 || stream->offset < stream->payload_end) {
			return -EBADMSG;
		}
		stream->complete = true;
	}
	return 0;
}

int spotflow_ota_mcuboot_stream_get_build_id(const struct spotflow_ota_mcuboot_stream* stream,
					     uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	if (stream == NULL || build_id == NULL) {
		return -EINVAL;
	}
	if (!stream->complete) {
		return -EBADMSG;
	}
	return spotflow_ota_identity_stream_get(&stream->identity, build_id);
}

static int read_header(struct spotflow_ota_mcuboot_stream* stream, size_t max_size)
{
	size_t header_size = sys_get_le16(stream->header + MCUBOOT_HEADER_SIZE_OFFSET);
	size_t image_size = sys_get_le32(stream->header + MCUBOOT_IMAGE_SIZE_OFFSET);
	uint32_t flags = sys_get_le32(stream->header + MCUBOOT_FLAGS_OFFSET);

	if (sys_get_le32(stream->header) != MCUBOOT_IMAGE_MAGIC ||
	    header_size < sizeof(stream->header) || header_size % 4 != 0 || image_size == 0 ||
	    header_size > max_size || image_size > max_size - header_size) {
		return -EBADMSG;
	}
	/* Encrypted, compressed and split images do not expose the running payload. */
	if ((flags & ~MCUBOOT_SUPPORTED_FLAGS) != 0) {
		return -ENOTSUP;
	}
	stream->payload_start = header_size;
	stream->payload_end = header_size + image_size;
	return 0;
}
