#ifndef SPOTFLOW_CONFIG_CBOR_H
#define SPOTFLOW_CONFIG_CBOR_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SPOTFLOW_CONFIG_RESPONSE_MAX_LENGTH 32

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPOTFLOW_DESIRED_FLAG_MINIMAL_LOG_SEVERITY = (1u << 0),
} spotflow_config_desired_flags_t;

typedef enum {
	SPOTFLOW_REPORTED_SETTINGS_FLAG_SENT_LOG_LEVEL = (1u << 0),
    SPOTFLOW_REPORTED_FLAG_COMPILED_MINIMAL_LOG_SEVERITY = (1u << 1),
    SPOTFLOW_REPORTED_FLAG_ACKED_DESIRED_CONFIG_VERSION = (1u << 2),
} spotflow_config_reported_flags_t;

struct spotflow_config_desired_msg {
	spotflow_config_desired_flags_t flags;
	uint32_t minimal_log_severity;
	uint64_t desired_config_version;
};

struct spotflow_config_reported_msg {
	spotflow_config_reported_flags_t flags;
	uint32_t minimal_log_severity;
	uint32_t compiled_minimal_log_severity;
	uint64_t acked_desired_config_version;
};

int spotflow_config_cbor_decode_desired(const uint8_t* payload, size_t len,
					struct spotflow_config_desired_msg* msg);
int spotflow_config_cbor_encode_reported(struct spotflow_config_reported_msg* msg, uint8_t* buffer,
					 size_t len, size_t* encoded_len);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_CONFIG_CBOR_H */