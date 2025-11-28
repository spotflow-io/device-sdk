#ifndef SPOTFLOW_COREDUMP_NET_H
#define SPOTFLOW_COREDUMP_NET_H

#define SPOTFLOW_MQTT_COREDUMP_TOPIC "ingest-cbor"

#define SPOTFLOW_MQTT_COREDUMP_QOS 1

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_coredump_send_message(void);

#ifdef __cplusplus
}
#endif

#endif