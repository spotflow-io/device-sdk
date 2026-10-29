/*
 * Copyright (c) 2025, Texas Instruments Incorporated - http://www.ti.com
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SPOTFLOW_TEST_TI_PSA_FWU_H
#define SPOTFLOW_TEST_TI_PSA_FWU_H

#include <fwu/1.0/psa/update.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Test-only TI component IDs and signed-container layout. */
typedef enum {
	BL2_Slot_1, /* ID 0 */
	BL2_Slot_2, /* ID 1 */
	WSOC_OR_RFTool_Slot_1, /* ID 2 */
	WSOC_OR_RFTool_Slot_2, /* ID 3 */
	Vendor_Image_Slot_1, /* ID 4 */
	Vendor_Image_Slot_2, /* ID 5 */
	MAX_COMPONENT_ID
} PSA_FWU_Component_ID_e;

#define MANIFEST_INTEGRITY_SIZE_IN_WORDS 4

/* This struct holds the MAGIC Number format entity  */
typedef struct {
	uint32_t Magic_Number;
} PSA_FWU_Magic_t;

/* This struct holds the GPE Header format entities */
typedef struct {
	PSA_FWU_Magic_t ih_magic;
	uint32_t ih_load_addr;
	uint16_t ih_hdr_size;
	uint16_t ih_protect_tlv_size;
	uint32_t ih_img_size;
	uint32_t ih_flags;
} PSA_FWU_GPEHeader_t;

/* This struct holds the GPE Version format entities */
typedef struct {
	uint8_t iv_major;
	uint8_t iv_minor;
	uint16_t iv_revision;
	uint32_t iv_build_num;
} PSA_FWU_GPEVersion_t;

/* This struct holds the GPE Manifest format entities */
typedef struct {
	PSA_FWU_GPEHeader_t GPE_Header;
	PSA_FWU_GPEVersion_t GPE_Version;
	PSA_FWU_Magic_t GPE_Magic;
} PSA_FWU_GPEManifest_t;

typedef struct {
	uint32_t Manifest_Integrity[MANIFEST_INTEGRITY_SIZE_IN_WORDS];
} PSA_FWU_Manifest_Integrity_t;

/* This struct holds the GPE Manifest entities */
typedef struct {
	PSA_FWU_Manifest_Integrity_t Manifest_Integrity;
	PSA_FWU_GPEManifest_t Manifest;
} PSA_FWU_GPESlot_t;

void psa_fwu_init(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_TEST_TI_PSA_FWU_H */
