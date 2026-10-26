#ifndef SPOTFLOW_OTA_PSA_FAKE_H
#define SPOTFLOW_OTA_PSA_FAKE_H

#include <psa_fwu.h>
#include <stdbool.h>

/** @brief State and injected results of the fake TI FWU service. */
struct spotflow_ota_psa_fake {
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

#ifdef __cplusplus
extern "C" {
#endif

extern struct spotflow_ota_psa_fake fake;
/** @brief Reset slots and all injected results to their initial state. */
void spotflow_ota_psa_fake_reset(void);
/** @brief Simulate a trial boot.
 * @param running Physical component now executing.
 */
void spotflow_ota_psa_fake_set_trial(psa_fwu_component_t running);
/** @brief Simulate durable confirmation.
 * @param running Physical component now executing.
 */
void spotflow_ota_psa_fake_set_confirmed(psa_fwu_component_t running);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_PSA_FAKE_H */
