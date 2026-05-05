#ifndef SPOTFLOW_TEST_COMMON_H
#define SPOTFLOW_TEST_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "test_wrapper.h"
#include "esp_log.h"
#include "cbor.h"
#include "logging/spotflow_log_backend.h"
#include "logging/spotflow_log_queue.h"
#include "logging/spotflow_log_cbor.h"
#include "net/spotflow_mqtt.h"

/**
 * @brief Check if a CBOR key-value pair exists in the given CBOR data
 *
 * @param cbor_data
 * @param cbor_len
 * @param key
 * @param expected_value
 * @return true
 * @return false
 */
bool contains_cbor_key(const uint8_t* cbor_data, size_t cbor_len, uint32_t key,
			      uint32_t expected_value);

#endif // SPOTFLOW_TEST_COMMON_H
