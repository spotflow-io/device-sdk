#ifndef SPOTFLOW_SESSION_METADATA_H
#define SPOTFLOW_SESSION_METADATA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_session_metadata_encode(uint8_t* buffer, size_t buffer_len, size_t* cbor_data_len);

/* Encodes the current session metadata for storing and replaying it in a later session. Unlike
 * spotflow_session_metadata_encode(), it never contains lastUpdateAttemptId (key 41). */
int spotflow_session_metadata_encode_for_replay(uint8_t* buffer, size_t buffer_len,
						size_t* cbor_data_len);

/* Returns the random run ID of the current boot, generating it on the first call. */
uint64_t spotflow_session_metadata_get_run_id(void);
int spotflow_session_metadata_send(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_SESSION_METADATA_H */
