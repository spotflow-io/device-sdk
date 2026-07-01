#ifndef SPOTFLOW_TRANSPORT_H
#define SPOTFLOW_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*spotflow_transport_message_cb)(uint8_t* payload, size_t len);

int spotflow_transport_start(void);
bool spotflow_transport_is_ready(void);
int spotflow_transport_send_ingest_cbor(uint8_t* payload, size_t len);
int spotflow_transport_send_config_cbor(uint8_t* payload, size_t len);
int spotflow_transport_subscribe_config(spotflow_transport_message_cb callback);
void spotflow_transport_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_TRANSPORT_H */
