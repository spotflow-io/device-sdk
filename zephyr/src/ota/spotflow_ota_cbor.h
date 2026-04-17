#ifndef SPOTFLOW_OTA_CBOR_H
#define SPOTFLOW_OTA_CBOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTFLOW_OTA_IMAGE_URL_MAX_LENGTH 64

/**
 * @brief Decoded payload of an OTA firmware update message
 */
struct spotflow_ota_update_firmware_msg {
	char image_url[SPOTFLOW_OTA_IMAGE_URL_MAX_LENGTH + 1];
};

/**
 * @brief Decode an OTA firmware update CBOR message
 *
 * @param payload Pointer to the raw CBOR payload
 * @param len     Length of the payload in bytes
 * @param msg     Output structure populated on success
 *
 * @return 0 on success, negative errno on failure
 *         -EINVAL: Null or empty payload, or malformed CBOR
 */
int spotflow_ota_cbor_decode_update_firmware(uint8_t* payload, size_t len,
					     struct spotflow_ota_update_firmware_msg* msg);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_CBOR_H */
