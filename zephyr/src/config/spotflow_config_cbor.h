#ifndef SPOTFLOW_CONFIG_CBOR_H
#define SPOTFLOW_CONFIG_CBOR_H

#include <stdbool.h>
#include <stdint.h>

#define SPOTFLOW_CONFIG_RESPONSE_MAX_LENGTH 32

#ifdef __cplusplus
extern "C" {
#endif

struct spotflow_config_desired_msg {
	bool contains_minimal_log_severity : 1;
	uint32_t minimal_log_severity;
	uint64_t desired_config_version;
};

struct spotflow_config_reported_msg {
	bool contains_minimal_log_severity : 1;
	bool contains_compiled_minimal_log_severity : 1;
	bool contains_acked_desired_config_version : 1;
	uint32_t minimal_log_severity;
	uint32_t compiled_minimal_log_severity;
	uint64_t acked_desired_config_version;
};

int spotflow_config_cbor_decode_desired(uint8_t* payload, size_t len,
					struct spotflow_config_desired_msg* msg);
int spotflow_config_cbor_encode_reported(struct spotflow_config_reported_msg* msg, uint8_t* buffer,
					 size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_CONFIG_CBOR_H */
