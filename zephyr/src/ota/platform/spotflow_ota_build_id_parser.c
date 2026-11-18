#include "ota/platform/spotflow_ota_build_id_parser.h"

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

void spotflow_ota_build_id_parser_init(struct spotflow_ota_build_id_parser* parser)
{
	if (parser != NULL) {
		memset(parser, 0, sizeof(*parser));
	}
}

void spotflow_ota_build_id_parser_feed(struct spotflow_ota_build_id_parser* parser,
				       const uint8_t* data, size_t len)
{
	if (parser == NULL) {
		return;
	}
	if (data == NULL && len != 0) {
		parser->error = -EINVAL;
		return;
	}

	for (size_t i = 0; i < len && parser->error == 0 && parser->phase != FOUND_ID; i++) {
		uint8_t byte = data[i];

		parser->payload_scan_offset++;
		switch (parser->phase) {
		case FIND_MAGIC:
			parser->magic = (parser->magic >> 8) | ((uint64_t)byte << 56);
			if (parser->payload_scan_offset >= sizeof(parser->magic) &&
			    parser->payload_scan_offset % BINDESC_ALIGNMENT == 0 &&
			    parser->magic == sys_cpu_to_le64(BINDESC_MAGIC)) {
				parser->phase = READ_HEADER;
			}
			break;
		case READ_HEADER:
			parser->header[parser->used++] = byte;
			if (parser->used == sizeof(parser->header)) {
				uint16_t tag;
				uint16_t size;

				/* Zephyr descriptors use the target's native byte order. */
				memcpy(&tag, parser->header, sizeof(tag));
				memcpy(&size, parser->header + sizeof(tag), sizeof(size));

				parser->used = 0;
				if (tag == BUILD_ID_TAG) {
					if (size != SPOTFLOW_BUILD_ID_LENGTH) {
						parser->error = -EINVAL;
					} else {
						parser->phase = READ_ID;
					}
				} else if (tag == BINDESC_TAG_DESCRIPTORS_END) {
					parser->error = -ENOENT;
				} else {
					parser->remaining = ROUND_UP(size, BINDESC_ALIGNMENT);
					parser->phase =
						parser->remaining ? SKIP_ENTRY : READ_HEADER;
				}
			}
			break;
		case READ_ID:
			parser->build_id[parser->used++] = byte;
			if (parser->used == sizeof(parser->build_id)) {
				uint8_t nonzero = 0;

				for (size_t j = 0; j < sizeof(parser->build_id); j++) {
					nonzero |= parser->build_id[j];
				}
				parser->error = nonzero ? 0 : -ENOSYS;
				parser->phase = FOUND_ID;
			}
			break;
		case SKIP_ENTRY:
			if (--parser->remaining == 0) {
				parser->phase = READ_HEADER;
			}
			break;
		default:
			break;
		}
	}
}

int spotflow_ota_build_id_parser_get_build_id(const struct spotflow_ota_build_id_parser* parser,
					      uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	if (parser == NULL || build_id == NULL) {
		return -EINVAL;
	}

	if (parser->error != 0) {
		return parser->error;
	}
	if (parser->phase != FOUND_ID) {
		return -ENOENT;
	}
	memcpy(build_id, parser->build_id, sizeof(parser->build_id));
	return 0;
}
