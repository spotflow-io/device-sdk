#include "spotflow_tasks.h"
#include <stddef.h>
#include "../queue/spotflow_queue.h"
#include "../transport_uart/spotflow_transport_uart.h"
#include "spotflow_config.h"
#include "spotflow_port.h"

typedef struct {
    SpotflowFrame *(*fetch)(uint32_t wait_ms);
    void (*recycle)(SpotflowFrame *frame);
} SpotflowSourceOps;

static SpotflowFrame *spotflow_log_fetch(uint32_t wait_ms)
{
    return spotflow_queue_consume(wait_ms);
}

static void spotflow_log_recycle(SpotflowFrame *frame)
{
    spotflow_queue_release_frame(frame);
}

static const SpotflowSourceOps s_sources[] = {
    { spotflow_log_fetch, spotflow_log_recycle },
};

static osThreadId s_sender_thread = NULL;

// Sender loop that drains queued frames and hands them to the transport backend.
static void SpotflowSenderTask(void const *argument)
{
    (void)argument;
    for (;;) {
        bool processed = false;

        for (size_t i = 0; i < SPOTFLOW_ARRAY_SIZE(s_sources); ++i) {
            uint32_t wait = (i == 0U) ? SPOTFLOW_SENDER_DEQUEUE_TIMEOUT_MS : 0U;
            SpotflowFrame *frame = s_sources[i].fetch(wait);
            if (frame != NULL) {
                bool sent = spotflow_transport_send_frame(frame);
                if (sent) {
                    spotflow_metrics_record_send();
                } else {
                    spotflow_metrics_record_drop();
                }
                s_sources[i].recycle(frame);
                processed = true;
            }
        }

        if (!processed) {
            spotflow_port_delay(SPOTFLOW_SENDER_IDLE_DELAY_MS);
        }
    }
}

osThreadDef(SpotflowSender, SpotflowSenderTask, SPOTFLOW_SENDER_TASK_PRIORITY, 0,
            SPOTFLOW_SENDER_TASK_STACK_SIZE);

bool spotflow_tasks_start(void)
{
    if (s_sender_thread != NULL) {
        return true;
    }

    s_sender_thread = osThreadCreate(osThread(SpotflowSender), NULL);
    return (s_sender_thread != NULL);
}
