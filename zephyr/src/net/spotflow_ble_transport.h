#ifndef SPOTFLOW_BLE_TRANSPORT_H
#define SPOTFLOW_BLE_TRANSPORT_H

#include "net/spotflow_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_ble_transport_start(void);
bool spotflow_ble_transport_is_ready(void);
bool spotflow_ble_transport_supports_feature(enum spotflow_transport_feature feature);
int spotflow_ble_transport_send_ingest_cbor(uint8_t* payload, size_t len);
int spotflow_ble_transport_send_config_cbor(uint8_t* payload, size_t len);
int spotflow_ble_transport_subscribe_config(spotflow_transport_message_cb callback);
void spotflow_ble_transport_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_BLE_TRANSPORT_H */
