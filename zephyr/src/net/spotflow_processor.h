#ifndef SPOTFLOW_PROCESSOR_H
#define SPOTFLOW_PROCESSOR_H

#ifdef __cplusplus
extern "C"
{
#endif

extern struct k_msgq g_spotflow_mqtt_msgq;

struct spotflow_mqtt_msg
{
	uint8_t *payload;
	size_t len;
};

void spotflow_start_mqtt(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_PROCESSOR_H */
