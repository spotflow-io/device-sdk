#ifndef SPOTFLOW_CONFIG_H
#define SPOTFLOW_CONFIG_H

/**
 * Central place for all Spotflow configuration knobs.
 * Adjust values here instead of scattering #defines across the project.
 */

#include "cmsis_os.h"
#include <stdint.h>

/* Queue + buffer sizing --------------------------------------------------- */
#ifndef SPOTFLOW_QUEUE_DEPTH
#define SPOTFLOW_QUEUE_DEPTH              (8U)
#endif

#ifndef SPOTFLOW_MAX_FRAME_SIZE
#define SPOTFLOW_MAX_FRAME_SIZE           (192U)
#endif

#ifndef SPOTFLOW_MAX_MSG_LEN
#define SPOTFLOW_MAX_MSG_LEN              (128U)
#endif

#ifndef SPOTFLOW_MAX_TAG_LEN
#define SPOTFLOW_MAX_TAG_LEN              (16U)
#endif

/* Producer behaviour ------------------------------------------------------ */
#ifndef SPOTFLOW_LOG_API_TIMEOUT_MS
#define SPOTFLOW_LOG_API_TIMEOUT_MS       (5U)   /* 0 => non-blocking */
#endif

#ifndef SPOTFLOW_DEFAULT_LEVEL_VALUE
#define SPOTFLOW_DEFAULT_LEVEL_VALUE      (2U)   /* maps to INFO */
#endif

/* Sender task tuning ------------------------------------------------------ */
#ifndef SPOTFLOW_SENDER_TASK_STACK_SIZE
#define SPOTFLOW_SENDER_TASK_STACK_SIZE   (384U)
#endif

#ifndef SPOTFLOW_SENDER_TASK_PRIORITY
#define SPOTFLOW_SENDER_TASK_PRIORITY     (osPriorityBelowNormal)
#endif

#ifndef SPOTFLOW_SENDER_DEQUEUE_TIMEOUT_MS
#define SPOTFLOW_SENDER_DEQUEUE_TIMEOUT_MS (50U)
#endif

#ifndef SPOTFLOW_SENDER_IDLE_DELAY_MS
#define SPOTFLOW_SENDER_IDLE_DELAY_MS     (5U)
#endif

/* UART transport ---------------------------------------------------------- */
#ifndef SPOTFLOW_UART_TIMEOUT_MS
#define SPOTFLOW_UART_TIMEOUT_MS          (50U)
#endif

/* Future transport selection ---------------------------------------------- */
#define SPOTFLOW_TRANSPORT_UART           (0U)
#define SPOTFLOW_TRANSPORT_SPI            (1U)

#ifndef SPOTFLOW_TRANSPORT_BACKEND
#define SPOTFLOW_TRANSPORT_BACKEND        (SPOTFLOW_TRANSPORT_UART)
#endif

#endif /* SPOTFLOW_CONFIG_H */
