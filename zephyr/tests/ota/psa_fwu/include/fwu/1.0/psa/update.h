// SPDX-FileCopyrightText: Copyright 2020-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef SPOTFLOW_TEST_PSA_UPDATE_H
#define SPOTFLOW_TEST_PSA_UPDATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Test-only subset of TI's PSA FWU 1.0 API, including its impl fields.
 * Production sources use the real platform headers. These declarations allow
 * host and QEMU tests to run without fetching the TI HAL module.
 */
#define PSA_FWU_API_VERSION_MAJOR 1
#define PSA_FWU_API_VERSION_MINOR 0

typedef int32_t psa_status_t;

#define PSA_SUCCESS ((psa_status_t)0)
#define PSA_ERROR_PROGRAMMER_ERROR ((psa_status_t) - 129)
#define PSA_ERROR_CONNECTION_REFUSED ((psa_status_t) - 130)
#define PSA_ERROR_CONNECTION_BUSY ((psa_status_t) - 131)
#define PSA_ERROR_GENERIC_ERROR ((psa_status_t) - 132)
#define PSA_ERROR_NOT_PERMITTED ((psa_status_t) - 133)
#define PSA_ERROR_NOT_SUPPORTED ((psa_status_t) - 134)
#define PSA_ERROR_INVALID_ARGUMENT ((psa_status_t) - 135)
#define PSA_ERROR_INVALID_HANDLE ((psa_status_t) - 136)
#define PSA_ERROR_BAD_STATE ((psa_status_t) - 137)
#define PSA_ERROR_BUFFER_TOO_SMALL ((psa_status_t) - 138)
#define PSA_ERROR_ALREADY_EXISTS ((psa_status_t) - 139)
#define PSA_ERROR_DOES_NOT_EXIST ((psa_status_t) - 140)
#define PSA_ERROR_INSUFFICIENT_MEMORY ((psa_status_t) - 141)
#define PSA_ERROR_INSUFFICIENT_STORAGE ((psa_status_t) - 142)
#define PSA_ERROR_INSUFFICIENT_DATA ((psa_status_t) - 143)
#define PSA_ERROR_SERVICE_FAILURE ((psa_status_t) - 144)
#define PSA_ERROR_COMMUNICATION_FAILURE ((psa_status_t) - 145)
#define PSA_ERROR_STORAGE_FAILURE ((psa_status_t) - 146)
#define PSA_ERROR_HARDWARE_FAILURE ((psa_status_t) - 147)
#define PSA_ERROR_INVALID_SIGNATURE ((psa_status_t) - 149)
#define PSA_ERROR_CORRUPTION_DETECTED ((psa_status_t) - 151)
#define PSA_ERROR_DATA_CORRUPT ((psa_status_t) - 152)
#define PSA_ERROR_DATA_INVALID ((psa_status_t) - 153)
#define PSA_OPERATION_INCOMPLETE ((psa_status_t) - 248)
#define PSA_SUCCESS_REBOOT ((psa_status_t)1)
#define PSA_SUCCESS_RESTART ((psa_status_t)2)

typedef uint8_t psa_fwu_component_t;

typedef struct psa_fwu_image_version_t {
	uint8_t major;
	uint8_t minor;
	uint16_t patch;
	uint32_t build;
} psa_fwu_image_version_t;

#define PSA_FWU_READY 0u

#define PSA_FWU_WRITING 1u

#define PSA_FWU_CANDIDATE 2u

#define PSA_FWU_STAGED 3u

#define PSA_FWU_FAILED 4u

#define PSA_FWU_TRIAL 5u

#define PSA_FWU_REJECTED 6u

#define PSA_FWU_UPDATED 7u

#define PSA_FWU_FLAG_VOLATILE_STAGING 0x00000001u

#define PSA_FWU_FLAG_ENCRYPTION 0x00000002u

typedef struct {
	uint8_t Primary;
	uint8_t running_status;
	uint8_t request_type;
	uint8_t extended_report_status;
	uint8_t GPE_state;
} psa_fwu_impl_info_t;

typedef struct psa_fwu_component_info_t {
	uint8_t state;
	psa_status_t error;
	psa_fwu_image_version_t version;
	uint32_t max_size;
	uint32_t flags;
	uint32_t location;
	psa_fwu_impl_info_t impl;
} psa_fwu_component_info_t;

psa_status_t psa_fwu_query(psa_fwu_component_t component, psa_fwu_component_info_t* info);
psa_status_t psa_fwu_start(psa_fwu_component_t component, const void* manifest,
			   size_t manifest_size);
psa_status_t psa_fwu_write(psa_fwu_component_t component, size_t image_offset, const void* block,
			   size_t block_size);
psa_status_t psa_fwu_finish(psa_fwu_component_t component);
psa_status_t psa_fwu_cancel(psa_fwu_component_t component);
psa_status_t psa_fwu_clean(psa_fwu_component_t component);
psa_status_t psa_fwu_install(void);
psa_status_t psa_fwu_request_reboot(void);
psa_status_t psa_fwu_reject(psa_status_t error);
psa_status_t psa_fwu_accept(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_TEST_PSA_UPDATE_H */
