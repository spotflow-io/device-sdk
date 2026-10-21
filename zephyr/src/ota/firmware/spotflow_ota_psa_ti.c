#include "ota/firmware/spotflow_ota_psa.h"
#include "ota/firmware/spotflow_ota_psa_identity.h"

#include <psa_fwu.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_OTA_LOG_LEVEL);

#define TI_PRIMARY 1
#define TI_RUNNING 1
#define TI_SECTOR_SIZE 4096U
#define TI_WIRE_OVERHEAD (TI_SECTOR_SIZE - 4U - 16U)
#define TI_IMAGE_MAGIC 0x690c47c2U
#define TI_PROTECTED_TLV_MAGIC 0x6908U
#define TI_TLV_MAGIC 0x6907U
#define TI_WRITE_CHUNK 256U

BUILD_ASSERT(sizeof(PSA_FWU_GPESlot_t) == 48);

/* Generated from the selected TI flash profile, not PSA's unpopulated max_size. */
extern uint32_t vendor_image_slot_1_region_size;
extern uint32_t vendor_image_slot_2_region_size;

static struct {
	PSA_FWU_GPESlot_t prefix;
	struct spotflow_ota_psa_identity identity;
	psa_fwu_component_t component;
	size_t offset;
	size_t max_size;
	size_t payload_end;
	size_t protected_end;
	size_t total_size;
	uint8_t protected_header[4];
	uint8_t tlv_header[4];
	bool selected;
	bool started;
	bool complete;
} stream;

int spotflow_ota_psa_init(void)
{
	psa_fwu_init();
	return 0;
}

static int query(psa_fwu_component_t component, psa_fwu_component_info_t* info)
{
	memset(info, 0, sizeof(*info));
	return spotflow_ota_psa_errno(psa_fwu_query(component, info));
}

static bool settled(uint8_t state)
{
	return state == PSA_FWU_READY || state == PSA_FWU_UPDATED || state == PSA_FWU_FAILED;
}

static int vendor_info(psa_fwu_component_info_t info[2])
{
	for (size_t i = 0; i < 2; i++) {
		int rc = query(Vendor_Image_Slot_1 + i, &info[i]);

		if (rc < 0) {
			return rc;
		}
	}
	if ((info[0].impl.running_status == TI_RUNNING) ==
	    (info[1].impl.running_status == TI_RUNNING) ||
	    (info[0].impl.Primary == TI_PRIMARY) == (info[1].impl.Primary == TI_PRIMARY)) {
		return -EIO;
	}
	return 0;
}

int spotflow_ota_psa_running_info(psa_fwu_component_info_t* info)
{
	psa_fwu_component_info_t slots[2];
	int rc = vendor_info(slots);

	if (rc == 0) {
		*info = slots[slots[0].impl.running_status == TI_RUNNING ? 0 : 1];
	}
	return rc;
}

/* install and accept apply globally. Refuse unrelated staged images or trials
 * rather than installing/accepting another client's update by accident.
 */
static int check_other_components(psa_fwu_component_t owned, uint8_t expected)
{
	for (psa_fwu_component_t id = 0; id < MAX_COMPONENT_ID; id++) {
		psa_fwu_component_info_t info;
		int rc = query(id, &info);

		if (rc < 0) {
			return rc;
		}
		if (id == owned) {
			if (info.state != expected || info.impl.Primary == TI_PRIMARY) {
				return -EINVAL;
			}
		} else if (!settled(info.state)) {
			LOG_WRN("PSA component %u in state %u blocks the global operation", id,
				info.state);
			return -EBUSY;
		}
	}
	return 0;
}

int spotflow_ota_psa_check_install(psa_fwu_component_t component)
{
	if (component != Vendor_Image_Slot_1 && component != Vendor_Image_Slot_2) {
		return -EINVAL;
	}
	return check_other_components(component, PSA_FWU_CANDIDATE);
}

int spotflow_ota_psa_check_accept(void)
{
	psa_fwu_component_info_t slots[2];
	int rc = vendor_info(slots);

	if (rc < 0) {
		return rc;
	}
	psa_fwu_component_t running = slots[0].impl.running_status == TI_RUNNING ?
					     Vendor_Image_Slot_1 : Vendor_Image_Slot_2;

	return check_other_components(running, PSA_FWU_TRIAL);
}

static int clean_target(void)
{
	psa_fwu_component_info_t info;
	int rc = query(stream.component, &info);

	if (rc < 0) {
		return rc;
	}
	if (info.impl.Primary == TI_PRIMARY || info.impl.running_status == TI_RUNNING) {
		return -EPERM;
	}
	if (info.state == PSA_FWU_STAGED) {
		/* An interrupted installation can leave an inactive staged image.
		 * Reject only when this is the service's sole unfinished component.
		 */
		rc = check_other_components(stream.component, PSA_FWU_STAGED);
		if (rc < 0) {
			return rc;
		}
		rc = spotflow_ota_psa_errno(psa_fwu_reject(PSA_ERROR_GENERIC_ERROR));
		if (rc < 0) {
			return rc;
		}
		info.state = PSA_FWU_FAILED;
	}
	if (info.state == PSA_FWU_WRITING || info.state == PSA_FWU_CANDIDATE) {
		rc = spotflow_ota_psa_errno(psa_fwu_cancel(stream.component));
		if (rc < 0) {
			return rc;
		}
		info.state = PSA_FWU_FAILED;
	}
	if (info.state == PSA_FWU_FAILED || info.state == PSA_FWU_UPDATED) {
		return spotflow_ota_psa_errno(psa_fwu_clean(stream.component));
	}
	return info.state == PSA_FWU_READY ? 0 : -EBUSY;
}

int spotflow_ota_psa_abort(void)
{
	if (!stream.selected) {
		return 0;
	}
	int rc = clean_target();

	memset(&stream, 0, sizeof(stream));
	return rc;
}

int spotflow_ota_psa_begin(void)
{
	psa_fwu_component_info_t slots[2];
	int rc = vendor_info(slots);

	memset(&stream, 0, sizeof(stream));
	if (rc < 0) {
		return rc;
	}
	size_t running = slots[0].impl.running_status == TI_RUNNING ? 0 : 1;
	size_t target = running ^ 1;

	/* A trial's primary slot is needed for rollback and must never be erased. */
	if (slots[running].state != PSA_FWU_UPDATED ||
	    slots[running].impl.Primary != TI_PRIMARY ||
	    slots[target].impl.Primary == TI_PRIMARY ||
	    slots[target].impl.running_status == TI_RUNNING) {
		return -EBUSY;
	}
	for (psa_fwu_component_t id = 0; id < Vendor_Image_Slot_1; id++) {
		psa_fwu_component_info_t info;

		rc = query(id, &info);
		if (rc < 0 || !settled(info.state)) {
			if (rc == 0) {
				LOG_WRN("PSA component %u in state %u blocks the vendor update", id,
					info.state);
			}
			return rc < 0 ? rc : -EBUSY;
		}
	}
	size_t slot_size = target == 0 ? vendor_image_slot_1_region_size :
					vendor_image_slot_2_region_size;

	if (slot_size <= TI_WIRE_OVERHEAD || slot_size % TI_SECTOR_SIZE != 0 ||
	    vendor_image_slot_1_region_size <= TI_WIRE_OVERHEAD ||
	    vendor_image_slot_2_region_size <= TI_WIRE_OVERHEAD ||
	    vendor_image_slot_1_region_size != vendor_image_slot_2_region_size) {
		return -ENOSPC;
	}
	stream.component = Vendor_Image_Slot_1 + target;
	LOG_INF("PSA vendor update target: component %u", stream.component);
	stream.max_size = slot_size - TI_WIRE_OVERHEAD;
	stream.selected = true;
	return clean_target();
}

static int start_stream(void)
{
	const PSA_FWU_GPEHeader_t* header = &stream.prefix.Manifest.GPE_Header;
	size_t minimum = sizeof(stream.prefix) + 4U;

	if (header->ih_magic.Magic_Number != TI_IMAGE_MAGIC ||
	    stream.prefix.Manifest.GPE_Magic.Magic_Number != TI_IMAGE_MAGIC ||
	    header->ih_hdr_size != sizeof(PSA_FWU_GPEManifest_t) || header->ih_flags != 0 ||
	    header->ih_img_size == 0 ||
	    (header->ih_protect_tlv_size != 0 && header->ih_protect_tlv_size < 4) ||
	    minimum > stream.max_size || header->ih_img_size > stream.max_size - minimum ||
	    header->ih_protect_tlv_size > stream.max_size - minimum - header->ih_img_size) {
		return -EINVAL;
	}
	stream.payload_end = sizeof(stream.prefix) + header->ih_img_size;
	stream.protected_end = stream.payload_end + header->ih_protect_tlv_size;
	int rc = spotflow_ota_psa_errno(psa_fwu_start(stream.component, &stream.prefix,
						   sizeof(stream.prefix)));

	stream.started = rc == 0;
	return rc;
}

static int inspect_data(const uint8_t* data, size_t len)
{
	/* Build identity is read from executable bytes only, never signature TLVs. */
	if (stream.offset < stream.payload_end) {
		size_t payload_len = MIN(len, stream.payload_end - stream.offset);

		spotflow_ota_psa_identity_feed(&stream.identity, data, payload_len);
	}
	for (size_t i = 0; i < len; i++) {
		size_t offset = stream.offset + i;

		if (stream.protected_end != stream.payload_end && offset >= stream.payload_end &&
		    offset - stream.payload_end < sizeof(stream.protected_header)) {
			size_t index = offset - stream.payload_end;

			stream.protected_header[index] = data[i];
			if (index == 3 &&
			    (sys_get_le16(stream.protected_header) != TI_PROTECTED_TLV_MAGIC ||
			     sys_get_le16(stream.protected_header + 2) !=
				     stream.prefix.Manifest.GPE_Header.ih_protect_tlv_size)) {
				return -EBADMSG;
			}
		}
		if (offset >= stream.protected_end &&
		    offset - stream.protected_end < sizeof(stream.tlv_header)) {
			size_t index = offset - stream.protected_end;

			stream.tlv_header[index] = data[i];
			if (index == 3) {
				size_t tlv_size = sys_get_le16(stream.tlv_header + 2);

				if (sys_get_le16(stream.tlv_header) != TI_TLV_MAGIC || tlv_size < 4 ||
				    tlv_size > stream.max_size - stream.protected_end) {
					return -EBADMSG;
				}
				stream.total_size = stream.protected_end + tlv_size;
			}
		}
		if (stream.total_size != 0 && offset >= stream.total_size) {
			return -EFBIG;
		}
	}
	return 0;
}

int spotflow_ota_psa_write(const struct spotflow_artifact_block* block)
{
	if (!stream.selected || stream.complete || block == NULL ||
	    (block->data == NULL && block->data_len != 0) || block->offset != stream.offset) {
		return -EINVAL;
	}
	if (block->data_len > stream.max_size - stream.offset) {
		return -EFBIG;
	}
	const uint8_t* data = block->data;
	size_t remaining = block->data_len;

	while (remaining > 0) {
		size_t len;
		int rc;

		if (stream.offset < sizeof(stream.prefix)) {
			len = MIN(remaining, sizeof(stream.prefix) - stream.offset);
			memcpy((uint8_t*)&stream.prefix + stream.offset, data, len);
			stream.offset += len;
			if (stream.offset == sizeof(stream.prefix)) {
				rc = start_stream();
				if (rc < 0) {
					return rc;
				}
			}
		} else {
			len = MIN(remaining, TI_WRITE_CHUNK);
			rc = inspect_data(data, len);
			if (rc < 0) {
				return rc;
			}
			rc = spotflow_ota_psa_errno(psa_fwu_write(stream.component, stream.offset,
								 data, len));
			if (rc < 0) {
				return rc;
			}
			stream.offset += len;
		}
		remaining -= len;
		data += len;
	}
	if (block->is_last) {
		if (!stream.started || stream.total_size == 0 ||
		    stream.offset != stream.total_size || stream.offset % 4 != 0) {
			return -EBADMSG;
		}
		stream.complete = true;
	}
	return 0;
}

int spotflow_ota_psa_end(psa_fwu_component_t* component,
		       uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	if (!stream.complete) {
		return -EBADMSG;
	}
	int rc = spotflow_ota_psa_identity_get(&stream.identity, build_id);

	if (rc == 0) {
		*component = stream.component;
	}
	return rc;
}
