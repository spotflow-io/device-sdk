#ifndef SPOTFLOW_PORT_H
#define SPOTFLOW_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "spotflow_config.h"
#define cbor_malloc malloc
#define cbor_realloc realloc
#define cbor_free free

typedef void* spotflow_queue_t;
typedef void* spotflow_thread_t;

spotflow_queue_t spotflow_port_queue_create(size_t queue_length, size_t item_size);
int             spotflow_port_queue_put(spotflow_queue_t queue, const void *item);
int             spotflow_port_queue_get(spotflow_queue_t queue, void *item, uint32_t timeout_ms);

spotflow_thread_t spotflow_port_thread_create(void (*task_func)(void *), const char *name,
                                              uint32_t stack_size, uint32_t priority);


uint32_t spotflow_port_get_tick_count(void);
uint32_t spotflow_port_tick_to_ms(uint32_t ticks);



#if (SPOTFLOW_LOG_USE_UART == 1)
void spotflow_port_uart_transmit(const uint8_t *data, size_t length);
#endif

#ifdef __cplusplus
}
#endif

#endif // SPOTFLOW_PORT_H
