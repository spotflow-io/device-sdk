#include "spotflow_ota_psa_ti_image_parser.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define TI_IMAGE_MAGIC 0x690c47c2U
#define TI_PROTECTED_TLV_MAGIC 0x6908U
#define TI_TLV_MAGIC 0x6907U

BUILD_ASSERT(sizeof(PSA_FWU_GPESlot_t) == 48);

static int parse_prefix(struct spotflow_ota_ti_image_parser* parser);
static int inspect_image_body(struct spotflow_ota_ti_image_parser* parser, const uint8_t* data,
			      size_t len);
static bool copy_tlv_header(uint8_t header[4], size_t header_offset, const uint8_t* data,
			    size_t offset, size_t len);

int spotflow_ota_ti_image_parser_init(struct spotflow_ota_ti_image_parser* parser, size_t max_size)
{
	if (parser == NULL || max_size == 0) {
		return -EINVAL;
	}
	memset(parser, 0, sizeof(*parser));
	parser->max_size = max_size;
	return 0;
}

int spotflow_ota_ti_image_parser_feed(struct spotflow_ota_ti_image_parser* parser,
				      const struct spotflow_artifact_block* block)
{
	if (parser == NULL || parser->artifact_complete || block == NULL ||
	    (block->data == NULL && block->data_len != 0) ||
	    block->offset != parser->artifact_offset) {
		return -EINVAL;
	}
	if (parser->artifact_offset > parser->max_size ||
	    block->data_len > parser->max_size - parser->artifact_offset) {
		return -EFBIG;
	}
	const uint8_t* data = block->data;
	size_t remaining = block->data_len;

	while (remaining > 0) {
		size_t len;
		int rc;

		if (parser->artifact_offset < sizeof(parser->prefix)) {
			len = MIN(remaining, sizeof(parser->prefix) - parser->artifact_offset);
			memcpy((uint8_t*)&parser->prefix + parser->artifact_offset, data, len);
			parser->artifact_offset += len;
			if (parser->artifact_offset == sizeof(parser->prefix)) {
				rc = parse_prefix(parser);
				if (rc < 0) {
					return rc;
				}
			}
		} else {
			len = remaining;
			rc = inspect_image_body(parser, data, len);
			if (rc < 0) {
				return rc;
			}
			parser->artifact_offset += len;
		}
		remaining -= len;
		data += len;
	}
	if (block->is_last) {
		if (parser->total_size == 0 || parser->artifact_offset != parser->total_size ||
		    parser->artifact_offset % 4 != 0) {
			return -EBADMSG;
		}
		parser->artifact_complete = true;
	}
	return 0;
}

int spotflow_ota_ti_image_parser_get_build_id(const struct spotflow_ota_ti_image_parser* parser,
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

int spotflow_ota_ti_image_parser_get_prefix(const struct spotflow_ota_ti_image_parser* parser,
					    const PSA_FWU_GPESlot_t** prefix)
{
	if (parser == NULL || prefix == NULL) {
		return -EINVAL;
	}
	if (parser->payload_end == 0) {
		return -EAGAIN;
	}
	*prefix = &parser->prefix;
	return 0;
}

static int parse_prefix(struct spotflow_ota_ti_image_parser* parser)
{
	const PSA_FWU_GPEHeader_t* header = &parser->prefix.Manifest.GPE_Header;
	size_t minimum = sizeof(parser->prefix) + 4U;

	if (header->ih_magic.Magic_Number != TI_IMAGE_MAGIC ||
	    parser->prefix.Manifest.GPE_Magic.Magic_Number != TI_IMAGE_MAGIC ||
	    header->ih_hdr_size != sizeof(PSA_FWU_GPEManifest_t) || header->ih_flags != 0 ||
	    header->ih_img_size == 0 ||
	    (header->ih_protect_tlv_size != 0 && header->ih_protect_tlv_size < 4) ||
	    minimum > parser->max_size || header->ih_img_size > parser->max_size - minimum ||
	    header->ih_protect_tlv_size > parser->max_size - minimum - header->ih_img_size) {
		return -EINVAL;
	}
	parser->payload_end = sizeof(parser->prefix) + header->ih_img_size;
	parser->protected_end = parser->payload_end + header->ih_protect_tlv_size;
	return 0;
}

static int inspect_image_body(struct spotflow_ota_ti_image_parser* parser, const uint8_t* data,
			      size_t len)
{
	size_t offset = parser->artifact_offset;

	/* The build-ID parser stops scanning as soon as it finds the descriptor. */
	if (offset < parser->payload_end) {
		size_t payload_len = MIN(len, parser->payload_end - offset);

		spotflow_ota_build_id_parser_feed(&parser->build_id_parser, data, payload_len);
	}
	/* Only the two TLV headers need inspection after the executable payload. */
	if (parser->protected_end != parser->payload_end &&
	    copy_tlv_header(parser->protected_header, parser->payload_end, data, offset, len)) {
		if (sys_get_le16(parser->protected_header) != TI_PROTECTED_TLV_MAGIC ||
		    sys_get_le16(parser->protected_header + 2) !=
			    parser->prefix.Manifest.GPE_Header.ih_protect_tlv_size) {
			return -EBADMSG;
		}
	}
	if (copy_tlv_header(parser->tlv_header, parser->protected_end, data, offset, len)) {
		size_t tlv_size = sys_get_le16(parser->tlv_header + 2);

		if (sys_get_le16(parser->tlv_header) != TI_TLV_MAGIC || tlv_size < 4 ||
		    tlv_size > parser->max_size - parser->protected_end) {
			return -EBADMSG;
		}
		parser->total_size = parser->protected_end + tlv_size;
	}
	if (parser->total_size != 0 && offset + len > parser->total_size) {
		return -EFBIG;
	}
	return 0;
}

/* Copy just the intersection with a four-byte TLV header. Return true when this
 * block completes it, including when the header spans multiple blocks.
 */
static bool copy_tlv_header(uint8_t header[4], size_t header_offset, const uint8_t* data,
			    size_t offset, size_t len)
{
	size_t start = MAX(offset, header_offset);
	size_t end = MIN(offset + len, header_offset + 4U);

	if (start >= end) {
		return false;
	}
	memcpy(header + start - header_offset, data + start - offset, end - start);
	return end == header_offset + 4U;
}
