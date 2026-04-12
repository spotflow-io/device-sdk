#include "port/port.h"
#include <string.h>
#include "cmsis_os2.h"

#if (SPOTFLOW_LOG_USE_UART == 1)
//#include "usart.h"
//extern UART_HandleTypeDef SPOTFLOW_LOG_UART_HANDLE;
#endif

/**
 * @brief Queue Abstractions. Create Queue
 *
 * @param queue_length
 * @param item_size
 * @return spotflow_queue_t
 */
spotflow_queue_t spotflow_port_queue_create(size_t queue_length, size_t item_size)
{
    return (spotflow_queue_t)osMessageQueueNew(queue_length, item_size, NULL);
}

/**
 * @brief Put item/data in queue.
 *
 * @param queue
 * @param item
 * @return int
 */
int spotflow_port_queue_put(spotflow_queue_t queue, const void *item)
{
    if (queue == NULL) return -1;
    return (osMessageQueuePut((osMessageQueueId_t)queue, item, 0, 0) == osOK) ? 0 : -1;
}

/**
 * @brief Get item/data from queue with timeout.
 *
 * @param queue
 * @param item
 * @param timeout_ms
 * @return int
 */
int spotflow_port_queue_get(spotflow_queue_t queue, void *item, uint32_t timeout_ms)
{
    if (queue == NULL) return -1;

    uint32_t timeout_ticks = (timeout_ms == 0) ? osWaitForever : timeout_ms;
    return (osMessageQueueGet((osMessageQueueId_t)queue, item, NULL, timeout_ticks) == osOK) ? 0 : -1;
}

/**
 * @brief Create a thread/task
 *
 * @param task_func
 * @param name
 * @param stack_size
 * @param priority
 * @return spotflow_thread_t
 */
spotflow_thread_t spotflow_port_thread_create(void (*task_func)(void *),
                                              const char *name,
                                              uint32_t stack_size,
                                              uint32_t priority)
{
    osThreadAttr_t attr = {
        .name = name,
        .stack_size = stack_size,
        .priority = (osPriority_t)priority
    };

    return (spotflow_thread_t)osThreadNew(task_func, NULL, &attr);
}

/**
 * @brief Get tick count.
 *
 * @return uint32_t
 */
uint32_t spotflow_port_get_tick_count(void)
{
    return osKernelGetTickCount();
}

/**
 * @brief Convert ticks to milliseconds.
 *
 * @param ticks
 * @return uint32_t
 */
uint32_t spotflow_port_tick_to_ms(uint32_t ticks)
{
    return (ticks * 1000U) / osKernelGetTickFreq();
}

/**
 * @brief Currently not used, but can be implemented to transmit logs over UART if needed.
 *
 */
#if (SPOTFLOW_LOG_USE_UART == 1)
//void spotflow_port_uart_transmit(const uint8_t *data, size_t length)
//{
//    HAL_UART_Transmit(&SPOTFLOW_LOG_UART_HANDLE, (uint8_t *)data, length, HAL_MAX_DELAY);
//}
#endif
