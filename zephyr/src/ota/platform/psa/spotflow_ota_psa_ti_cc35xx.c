#include "ota/platform/psa/spotflow_ota_psa_provider.h"
#include "spotflow_ota_psa_ti_container.h"

#include <psa_fwu.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_OTA_LOG_LEVEL);

#define TI_PRIMARY 1
#define TI_RUNNING 1
#define TI_WRITE_CHUNK_SIZE 256U

struct spotflow_ota_ti_download {
	struct spotflow_ota_ti_container container;
	psa_fwu_component_t component;
	bool selected;
	bool started;
};

/* Generated from the selected TI flash profile, not PSA's unpopulated max_size. */
extern uint32_t vendor_image_slot_1_region_size;
extern uint32_t vendor_image_slot_2_region_size;

static struct spotflow_ota_ti_download download;
static const psa_fwu_component_t vendor_components[] = {
	Vendor_Image_Slot_1,
	Vendor_Image_Slot_2,
};

static int query_component(psa_fwu_component_t component, psa_fwu_component_info_t* info);
static bool component_state_is_idle(uint8_t state);
static int query_vendor_slots(psa_fwu_component_info_t info[2]);
static int check_global_operation_ownership(psa_fwu_component_t owned, uint8_t expected);
static int clean_download_target(void);

int spotflow_ota_psa_init(void)
{
	psa_fwu_init();
	return 0;
}

int spotflow_ota_psa_running_info(psa_fwu_component_info_t* info)
{
	if (info == NULL) {
		return -EINVAL;
	}
	psa_fwu_component_info_t slots[2];
	int rc = query_vendor_slots(slots);

	if (rc == 0) {
		*info = slots[slots[0].impl.running_status == TI_RUNNING ? 0 : 1];
	}
	return rc;
}

int spotflow_ota_psa_check_install(psa_fwu_component_t component)
{
	if (component != Vendor_Image_Slot_1 && component != Vendor_Image_Slot_2) {
		return -EINVAL;
	}
	return check_global_operation_ownership(component, PSA_FWU_CANDIDATE);
}

int spotflow_ota_psa_check_accept(void)
{
	psa_fwu_component_info_t slots[2];
	int rc = query_vendor_slots(slots);

	if (rc < 0) {
		return rc;
	}
	psa_fwu_component_t running = slots[0].impl.running_status == TI_RUNNING
		? Vendor_Image_Slot_1
		: Vendor_Image_Slot_2;

	return check_global_operation_ownership(running, PSA_FWU_TRIAL);
}

int spotflow_ota_psa_abort(void)
{
	if (!download.selected) {
		return 0;
	}
	int rc = clean_download_target();

	memset(&download, 0, sizeof(download));
	return rc;
}

int spotflow_ota_psa_begin(void)
{
	psa_fwu_component_info_t slots[2];
	int rc = query_vendor_slots(slots);

	memset(&download, 0, sizeof(download));
	if (rc < 0) {
		return rc;
	}
	size_t running = slots[0].impl.running_status == TI_RUNNING ? 0 : 1;
	/* Physical slot numbers are fixed; their running/primary roles alternate.
	 * This pairing belongs to TI, not to PSA component IDs in general.
	 */
	size_t target = running == 0 ? 1 : 0;

	/* A trial's primary slot is needed for rollback and must never be erased. */
	if (slots[running].state != PSA_FWU_UPDATED || slots[running].impl.Primary != TI_PRIMARY ||
	    slots[target].impl.Primary == TI_PRIMARY ||
	    slots[target].impl.running_status == TI_RUNNING) {
		return -EBUSY;
	}
	for (psa_fwu_component_t id = 0; id < Vendor_Image_Slot_1; id++) {
		psa_fwu_component_info_t info;

		rc = query_component(id, &info);
		if (rc < 0 || !component_state_is_idle(info.state)) {
			if (rc == 0) {
				LOG_WRN("PSA component %u in state %u blocks the vendor update", id,
					info.state);
			}
			return rc < 0 ? rc : -EBUSY;
		}
	}
	size_t slot_size =
		target == 0 ? vendor_image_slot_1_region_size : vendor_image_slot_2_region_size;

	if (slot_size <= SPOTFLOW_OTA_TI_SLOT_OVERHEAD ||
	    slot_size % SPOTFLOW_OTA_TI_SECTOR_SIZE != 0 ||
	    vendor_image_slot_1_region_size <= SPOTFLOW_OTA_TI_SLOT_OVERHEAD ||
	    vendor_image_slot_2_region_size <= SPOTFLOW_OTA_TI_SLOT_OVERHEAD ||
	    vendor_image_slot_1_region_size != vendor_image_slot_2_region_size) {
		return -ENOSPC;
	}
	download.component = vendor_components[target];
	LOG_INF("PSA vendor update target: component %u", download.component);
	download.container.max_size = slot_size - SPOTFLOW_OTA_TI_SLOT_OVERHEAD;
	download.selected = true;
	return clean_download_target();
}

int spotflow_ota_psa_write(const struct spotflow_artifact_block* block)
{
	if (!download.selected) {
		return -EINVAL;
	}
	int rc = spotflow_ota_ti_container_feed(&download.container, block);

	if (rc < 0) {
		return rc;
	}
	size_t prefix_size = sizeof(download.container.prefix);

	if (download.container.offset < prefix_size) {
		return 0;
	}
	if (!download.started) {
		rc = spotflow_ota_psa_errno(
			psa_fwu_start(download.component, &download.container.prefix, prefix_size));
		if (rc < 0) {
			return rc;
		}
		download.started = true;
	}
	/* PSA start consumes the prefix. TI write offsets still include those bytes. */
	size_t offset = MAX(block->offset, prefix_size);
	size_t end = block->offset + block->data_len;

	while (offset < end) {
		size_t length = MIN(end - offset, TI_WRITE_CHUNK_SIZE);

		rc = spotflow_ota_psa_errno(psa_fwu_write(
			download.component, offset, block->data + offset - block->offset, length));
		if (rc < 0) {
			return rc;
		}
		offset += length;
	}
	return 0;
}

int spotflow_ota_psa_end(psa_fwu_component_t* component, uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	if (component == NULL || build_id == NULL) {
		return -EINVAL;
	}
	int rc = spotflow_ota_ti_container_get_build_id(&download.container, build_id);

	if (rc == 0) {
		*component = download.component;
	}
	return rc;
}

static int query_component(psa_fwu_component_t component, psa_fwu_component_info_t* info)
{
	memset(info, 0, sizeof(*info));
	return spotflow_ota_psa_errno(psa_fwu_query(component, info));
}

static bool component_state_is_idle(uint8_t state)
{
	return state == PSA_FWU_READY || state == PSA_FWU_UPDATED || state == PSA_FWU_FAILED;
}

static int query_vendor_slots(psa_fwu_component_info_t info[2])
{
	for (size_t i = 0; i < ARRAY_SIZE(vendor_components); i++) {
		int rc = query_component(vendor_components[i], &info[i]);

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

/* install/accept apply to the whole service. Include only Spotflow's candidate;
 * an unrelated writer, candidate or trial must block the operation.
 */
static int check_global_operation_ownership(psa_fwu_component_t owned, uint8_t expected)
{
	for (psa_fwu_component_t id = 0; id < MAX_COMPONENT_ID; id++) {
		psa_fwu_component_info_t info;
		int rc = query_component(id, &info);

		if (rc < 0) {
			return rc;
		}
		if (id == owned) {
			if (info.state != expected || info.impl.Primary == TI_PRIMARY) {
				return -EINVAL;
			}
		} else if (!component_state_is_idle(info.state)) {
			LOG_WRN("PSA component %u in state %u blocks the global operation", id,
				info.state);
			return -EBUSY;
		}
	}
	return 0;
}

static int clean_download_target(void)
{
	psa_fwu_component_info_t info;
	int rc = query_component(download.component, &info);

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
		rc = check_global_operation_ownership(download.component, PSA_FWU_STAGED);
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
		rc = spotflow_ota_psa_errno(psa_fwu_cancel(download.component));
		if (rc < 0) {
			return rc;
		}
		info.state = PSA_FWU_FAILED;
	}
	if (info.state == PSA_FWU_FAILED || info.state == PSA_FWU_UPDATED) {
		return spotflow_ota_psa_errno(psa_fwu_clean(download.component));
	}
	return info.state == PSA_FWU_READY ? 0 : -EBUSY;
}
