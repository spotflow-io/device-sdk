#ifndef SPOTFLOW_OTA_PSA_FAKE_H
#define SPOTFLOW_OTA_PSA_FAKE_H

#include <psa_fwu.h>
#include <stdbool.h>

struct psa_fake {
	psa_fwu_component_info_t slots[MAX_COMPONENT_ID];
	psa_status_t query_error;
	psa_status_t start_error;
	psa_status_t write_error;
	psa_status_t finish_error;
	psa_status_t install_result;
	psa_status_t accept_result;
	psa_status_t reboot_result;
	unsigned int init_count;
	unsigned int start_count;
	unsigned int write_count;
	unsigned int finish_count;
	unsigned int install_count;
	unsigned int accept_count;
	unsigned int reboot_count;
	unsigned int reject_count;
	unsigned int cancel_count;
	unsigned int clean_count;
	psa_fwu_component_t target;
	size_t next_write_offset;
	size_t max_block_size;
	uint8_t image[16384];
};

extern struct psa_fake fake;
void psa_fake_reset(void);
void psa_fake_trial(psa_fwu_component_t running);
void psa_fake_committed(psa_fwu_component_t running);

#endif
