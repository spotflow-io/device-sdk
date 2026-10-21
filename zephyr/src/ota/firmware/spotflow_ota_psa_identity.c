#include "ota/firmware/spotflow_ota_psa_identity.h"

#include <errno.h>
#include <string.h>
#include <zephyr/bindesc.h>
#include <zephyr/sys/byteorder.h>

#define BUILD_ID_TAG BINDESC_TAG(BYTES, 0x5f0)

enum parser_phase { FIND_MAGIC, READ_HEADER, READ_ID, SKIP_ENTRY, FOUND_ID };

void spotflow_ota_psa_identity_feed(struct spotflow_ota_psa_identity* ctx,
				   const uint8_t* data, size_t len)
{
	for (size_t i = 0; i < len && ctx->error == 0 && ctx->phase != FOUND_ID; i++) {
		uint8_t byte = data[i];

		ctx->offset++;
		switch (ctx->phase) {
		case FIND_MAGIC:
			ctx->magic = (ctx->magic >> 8) | ((uint64_t)byte << 56);
			if (ctx->offset >= sizeof(ctx->magic) &&
			    ctx->offset % BINDESC_ALIGNMENT == 0 && ctx->magic == BINDESC_MAGIC) {
				ctx->phase = READ_HEADER;
			}
			break;
		case READ_HEADER:
			ctx->header[ctx->used++] = byte;
			if (ctx->used == sizeof(ctx->header)) {
				uint16_t tag = sys_get_le16(ctx->header);
				uint16_t size = sys_get_le16(ctx->header + 2);

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

int spotflow_ota_psa_identity_get(const struct spotflow_ota_psa_identity* ctx,
				uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	if (ctx->error != 0) {
		return ctx->error;
	}
	if (ctx->phase != FOUND_ID) {
		return -ENOENT;
	}
	memcpy(build_id, ctx->build_id, sizeof(ctx->build_id));
	return 0;
}

bool spotflow_ota_psa_identity_matches(const uint8_t expected[SPOTFLOW_BUILD_ID_LENGTH])
{
	const uint8_t* running;
	uint16_t len;

	return spotflow_build_id_get(&running, &len) == 0 && len == SPOTFLOW_BUILD_ID_LENGTH &&
	       memcmp(expected, running, SPOTFLOW_BUILD_ID_LENGTH) == 0;
}
