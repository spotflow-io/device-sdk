#include "psa_fake.h"
#include <string.h>

struct psa_fake fake;
uint32_t vendor_image_slot_1_region_size = 16384;
uint32_t vendor_image_slot_2_region_size = 16384;

void psa_fake_reset(void)
{
	memset(&fake, 0, sizeof(fake));
	for (size_t i = 0; i < MAX_COMPONENT_ID; i += 2) {
		fake.slots[i].state = PSA_FWU_UPDATED;
		fake.slots[i].impl.Primary = 1;
		fake.slots[i].impl.running_status = 1;
	}
	fake.install_result = PSA_SUCCESS_REBOOT;
	fake.accept_result = PSA_SUCCESS_REBOOT;
	vendor_image_slot_1_region_size = 16384;
	vendor_image_slot_2_region_size = 16384;
}

void psa_fake_trial(psa_fwu_component_t running)
{
	fake.slots[running].state = PSA_FWU_TRIAL;
	fake.slots[running].impl.running_status = 1;
	fake.slots[running].impl.Primary = 0;
	fake.slots[running ^ 1].state = PSA_FWU_UPDATED;
	fake.slots[running ^ 1].impl.running_status = 0;
	fake.slots[running ^ 1].impl.Primary = 1;
}

void psa_fake_committed(psa_fwu_component_t running)
{
	fake.slots[running].state = PSA_FWU_UPDATED;
	fake.slots[running].impl.running_status = 1;
	fake.slots[running].impl.Primary = 1;
	fake.slots[running ^ 1].state = PSA_FWU_FAILED;
	fake.slots[running ^ 1].impl.running_status = 0;
	fake.slots[running ^ 1].impl.Primary = 0;
}

void psa_fwu_init(void)
{
	fake.init_count++;
}

psa_status_t psa_fwu_query(psa_fwu_component_t component, psa_fwu_component_info_t* info)
{
	if (fake.query_error != PSA_SUCCESS) {
		return fake.query_error;
	}
	*info = fake.slots[component];
	return PSA_SUCCESS;
}

psa_status_t psa_fwu_start(psa_fwu_component_t component, const void* manifest, size_t len)
{
	fake.start_count++;
	fake.target = component;
	if (fake.start_error != PSA_SUCCESS) {
		return fake.start_error;
	}
	if (len != sizeof(PSA_FWU_GPESlot_t) || fake.slots[component].state != PSA_FWU_READY) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}
	memcpy(fake.image, manifest, len);
	fake.next_write_offset = len;
	fake.slots[component].state = PSA_FWU_WRITING;
	return PSA_SUCCESS;
}

psa_status_t psa_fwu_write(psa_fwu_component_t component, size_t offset,
			   const void* data, size_t len)
{
	fake.write_count++;
	if (fake.write_error != PSA_SUCCESS) {
		return fake.write_error;
	}
	if (component != fake.target || offset != fake.next_write_offset || len == 0 ||
	    len > sizeof(fake.image) - offset) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}
	memcpy(fake.image + offset, data, len);
	fake.next_write_offset += len;
	if (len > fake.max_block_size) {
		fake.max_block_size = len;
	}
	return PSA_SUCCESS;
}

psa_status_t psa_fwu_finish(psa_fwu_component_t component)
{
	fake.finish_count++;
	if (fake.finish_error != PSA_SUCCESS) {
		return fake.finish_error;
	}
	fake.slots[component].state = PSA_FWU_CANDIDATE;
	return PSA_SUCCESS;
}

psa_status_t psa_fwu_install(void)
{
	fake.install_count++;
	if (fake.install_result == PSA_SUCCESS_REBOOT) {
		fake.slots[fake.target].state = PSA_FWU_STAGED;
	}
	return fake.install_result;
}

psa_status_t psa_fwu_accept(void)
{
	fake.accept_count++;
	if (fake.accept_result == PSA_SUCCESS) {
		psa_fake_committed(fake.slots[4].impl.running_status ? 4 : 5);
	}
	return fake.accept_result;
}

psa_status_t psa_fwu_request_reboot(void)
{
	fake.reboot_count++;
	return fake.reboot_result;
}

psa_status_t psa_fwu_cancel(psa_fwu_component_t component)
{
	fake.cancel_count++;
	fake.slots[component].state = PSA_FWU_FAILED;
	return PSA_SUCCESS;
}

psa_status_t psa_fwu_clean(psa_fwu_component_t component)
{
	fake.clean_count++;
	fake.slots[component].state = PSA_FWU_READY;
	return PSA_SUCCESS;
}

psa_status_t psa_fwu_reject(psa_status_t error)
{
	fake.reject_count++;
	for (size_t i = 0; i < MAX_COMPONENT_ID; i++) {
		if (fake.slots[i].state == PSA_FWU_STAGED) {
			fake.slots[i].state = PSA_FWU_FAILED;
			fake.slots[i].error = error;
		}
	}
	return PSA_SUCCESS;
}
