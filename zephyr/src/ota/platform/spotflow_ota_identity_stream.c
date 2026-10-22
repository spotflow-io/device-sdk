#include "ota/platform/spotflow_ota_identity_stream.h"

#include <errno.h>
#include <string.h>
#include <zephyr/bindesc.h>
#include <zephyr/sys/byteorder.h>

#define BUILD_ID_TAG BINDESC_TAG(BYTES, 0x5f0)

enum parser_phase {
	FIND_MAGIC,
	READ_HEADER,
	READ_ID,
	SKIP_ENTRY,
	FOUND_ID,
};

void spotflow_ota_identity_stream_feed(struct spotflow_ota_identity_stream* ctx,
				       const uint8_t* data, size_t len)
{
	if (ctx == NULL) {
		return;
	}
	if (data == NULL && len != 0) {
		ctx->error = -EINVAL;
		return;
	}

	for (size_t i = 0; i < len && ctx->error == 0 && ctx->phase != FOUND_ID; i++) {
		uint8_t byte = data[i];

		ctx->offset++;
		switch (ctx->phase) {
		case FIND_MAGIC:
			ctx->magic = (ctx->magic >> 8) | ((uint64_t)byte << 56);
			if (ctx->offset >= sizeof(ctx->magic) &&
			    ctx->offset % BINDESC_ALIGNMENT == 0 &&
			    ctx->magic == sys_cpu_to_le64(BINDESC_MAGIC)) {
				ctx->phase = READ_HEADER;
			}
			break;
		case READ_HEADER:
			ctx->header[ctx->used++] = byte;
			if (ctx->used == sizeof(ctx->header)) {
				uint16_t tag;
				uint16_t size;

				/* Zephyr descriptors use the target's native byte order. */
				memcpy(&tag, ctx->header, sizeof(tag));
				memcpy(&size, ctx->header + sizeof(tag), sizeof(size));

				ctx->used = 0;
				if (tag == BUILD_ID_TAG) {
					if (size != SPOTFLOW_BUILD_ID_LENGTH) {
						ctx->error = -EINVAL;
					} else {
						ctx->phase = READ_ID;
					}
				} else if (tag == BINDESC_TAG_DESCRIPTORS_END) {
					ctx->error = -ENOENT;
				} else {
					ctx->remaining = ROUND_UP(size, BINDESC_ALIGNMENT);
					ctx->phase = ctx->remaining ? SKIP_ENTRY : READ_HEADER;
				}
			}
			break;
		case READ_ID:
			ctx->build_id[ctx->used++] = byte;
			if (ctx->used == sizeof(ctx->build_id)) {
				uint8_t nonzero = 0;

				for (size_t j = 0; j < sizeof(ctx->build_id); j++) {
					nonzero |= ctx->build_id[j];
				}
				ctx->error = nonzero ? 0 : -ENOSYS;
				ctx->phase = FOUND_ID;
			}
			break;
		case SKIP_ENTRY:
			if (--ctx->remaining == 0) {
				ctx->phase = READ_HEADER;
			}
			break;
		default:
			break;
		}
	}
}

int spotflow_ota_identity_stream_get(const struct spotflow_ota_identity_stream* ctx,
				     uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	if (ctx == NULL || build_id == NULL) {
		return -EINVAL;
	}

	if (ctx->error != 0) {
		return ctx->error;
	}
	if (ctx->phase != FOUND_ID) {
		return -ENOENT;
	}
	memcpy(build_id, ctx->build_id, sizeof(ctx->build_id));
	return 0;
}
