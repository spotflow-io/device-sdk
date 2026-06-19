#ifndef SPOTFLOW_OTA_TEST_WAIT_H
#define SPOTFLOW_OTA_TEST_WAIT_H

#include <stddef.h>
#include <stdint.h>

#include <spotflow/ota.h>

#include "ota/protocol/spotflow_ota_cbor.h"

#ifdef __cplusplus
extern "C" {
#endif

void spotflow_ota_test_wait_for_persisted_attempt(uint64_t attempt_id,
						  const enum spotflow_ota_result* expected_results,
						  size_t artifact_count);

void spotflow_ota_test_wait_for_persisted_attempt_error(
    uint64_t attempt_id, enum spotflow_ota_attempt_error attempt_error);

void spotflow_ota_test_expect_update_results_payload(
    const struct spotflow_ota_cbor_update_results* expected_message);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_TEST_WAIT_H */
