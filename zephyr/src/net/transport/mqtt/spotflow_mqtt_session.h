#ifndef SPOTFLOW_MQTT_SESSION_H
#define SPOTFLOW_MQTT_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*spotflow_mqtt_process_fn)(void);

void spotflow_mqtt_session_loop(spotflow_mqtt_process_fn process_fn);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_MQTT_SESSION_H */
