#include "spotflow_ota_psa_ti_container.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define TI_IMAGE_MAGIC 0x690c47c2U
#define TI_PROTECTED_TLV_MAGIC 0x6908U
#define TI_TLV_MAGIC 0x6907U

BUILD_ASSERT(sizeof(PSA_FWU_GPESlot_t) == 48);

static int read_prefix(struct spotflow_ota_ti_container* container);
static int inspect_payload(struct spotflow_ota_ti_container* container, const uint8_t* data,
			   size_t len);
static bool copy_tlv_header(uint8_t header[4], size_t header_offset, const uint8_t* data,
			    size_t offset, size_t len);

int spotflow_ota_ti_container_init(struct spotflow_ota_ti_container* container, size_t max_size)
{
	if (container == NULL || max_size == 0) {
		return -EINVAL;
	}
	memset(container, 0, sizeof(*container));
	container->max_size = max_size;
	return 0;
}

int spotflow_ota_ti_container_feed(struct spotflow_ota_ti_container* container,
				   const struct spotflow_artifact_block* block)
{
	if (container == NULL || container->complete || block == NULL ||
	    (block->data == NULL && block->data_len != 0) || block->offset != container->offset) {
		return -EINVAL;
	}
	if (container->offset > container->max_size ||
	    block->data_len > container->max_size - container->offset) {
		return -EFBIG;
	}
	const uint8_t* data = block->data;
	size_t remaining = block->data_len;

	while (remaining > 0) {
		size_t len;
		int rc;

		if (container->offset < sizeof(container->prefix)) {
			len = MIN(remaining, sizeof(container->prefix) - container->offset);
			memcpy((uint8_t*)&container->prefix + container->offset, data, len);
			container->offset += len;
			if (container->offset == sizeof(container->prefix)) {
				rc = read_prefix(container);
				if (rc < 0) {
					return rc;
				}
			}
		} else {
			len = remaining;
			rc = inspect_payload(container, data, len);
			if (rc < 0) {
				return rc;
			}
			container->offset += len;
		}
		remaining -= len;
		data += len;
	}
	if (block->is_last) {
		if (container->total_size == 0 || container->offset != container->total_size ||
		    container->offset % 4 != 0) {
			return -EBADMSG;
		}
		container->complete = true;
	}
	return 0;
}

int spotflow_ota_ti_container_get_build_id(const struct spotflow_ota_ti_container* container,
					   uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	if (container == NULL || build_id == NULL) {
		return -EINVAL;
	}
	if (!container->complete) {
		return -EBADMSG;
	}
	return spotflow_ota_identity_stream_get(&container->identity, build_id);
}

static int read_prefix(struct spotflow_ota_ti_container* container)
{
	const PSA_FWU_GPEHeader_t* header = &container->prefix.Manifest.GPE_Header;
	size_t minimum = sizeof(container->prefix) + 4U;

	if (header->ih_magic.Magic_Number != TI_IMAGE_MAGIC ||
	    container->prefix.Manifest.GPE_Magic.Magic_Number != TI_IMAGE_MAGIC ||
	    header->ih_hdr_size != sizeof(PSA_FWU_GPEManifest_t) || header->ih_flags != 0 ||
	    header->ih_img_size == 0 ||
	    (header->ih_protect_tlv_size != 0 && header->ih_protect_tlv_size < 4) ||
	    minimum > container->max_size || header->ih_img_size > container->max_size - minimum ||
	    header->ih_protect_tlv_size > container->max_size - minimum - header->ih_img_size) {
		return -EINVAL;
	}
	container->payload_end = sizeof(container->prefix) + header->ih_img_size;
	container->protected_end = container->payload_end + header->ih_protect_tlv_size;
	return 0;
}

static int inspect_payload(struct spotflow_ota_ti_container* container, const uint8_t* data,
			   size_t len)
{
	size_t offset = container->offset;

	/* The identity reader stops scanning as soon as it finds the build ID. */
	if (offset < container->payload_end) {
		size_t payload_len = MIN(len, container->payload_end - offset);

		spotflow_ota_identity_stream_feed(&container->identity, data, payload_len);
	}
	/* Only the two TLV headers need inspection after the executable payload. */
	if (container->protected_end != container->payload_end &&
	    copy_tlv_header(container->protected_header, container->payload_end, data, offset,
			    len)) {
		if (sys_get_le16(container->protected_header) != TI_PROTECTED_TLV_MAGIC ||
		    sys_get_le16(container->protected_header + 2) !=
			    container->prefix.Manifest.GPE_Header.ih_protect_tlv_size) {
			return -EBADMSG;
		}
	}
	if (copy_tlv_header(container->tlv_header, container->protected_end, data, offset, len)) {
		size_t tlv_size = sys_get_le16(container->tlv_header + 2);

		if (sys_get_le16(container->tlv_header) != TI_TLV_MAGIC || tlv_size < 4 ||
		    tlv_size > container->max_size - container->protected_end) {
			return -EBADMSG;
		}
		container->total_size = container->protected_end + tlv_size;
	}
	if (container->total_size != 0 && offset + len > container->total_size) {
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
