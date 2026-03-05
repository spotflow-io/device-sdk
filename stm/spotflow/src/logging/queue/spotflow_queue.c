#include "spotflow_queue.h"
#include <string.h>

#if UINTPTR_MAX > 0xFFFFFFFFUL
#error "Spotflow queue expects 32-bit pointers on STM32F1"
#endif

osMessageQDef(SpotflowLogQueue, SPOTFLOW_QUEUE_DEPTH, uint32_t);
osMutexDef(SpotflowQueuePoolLock);

typedef struct {
    SpotflowFrame frames[SPOTFLOW_QUEUE_DEPTH];
    SpotflowFrame *free_stack[SPOTFLOW_QUEUE_DEPTH];
    uint32_t free_top;
    osMutexId pool_lock;
    osMessageQId queue_id;
    bool initialized;
} SpotflowQueueCtx;

static SpotflowQueueCtx s_queue_ctx;

static uint32_t adjust_timeout(uint32_t timeout_ms)
{
    return timeout_ms;
}

bool spotflow_queue_init(void)
{
    if (s_queue_ctx.initialized) {
        return true;
    }

    memset(&s_queue_ctx, 0, sizeof(s_queue_ctx));

    s_queue_ctx.pool_lock = osMutexCreate(osMutex(SpotflowQueuePoolLock));
    if (s_queue_ctx.pool_lock == NULL) {
        return false;
    }

    for (uint32_t i = 0; i < SPOTFLOW_QUEUE_DEPTH; ++i) {
        s_queue_ctx.free_stack[i] = &s_queue_ctx.frames[i];
    }
    s_queue_ctx.free_top = SPOTFLOW_QUEUE_DEPTH;

    s_queue_ctx.queue_id = osMessageCreate(osMessageQ(SpotflowLogQueue), NULL);
    if (s_queue_ctx.queue_id == NULL) {
        return false;
    }

    s_queue_ctx.initialized = true;
    return true;
}

SpotflowFrame *spotflow_queue_acquire_frame(uint32_t timeout_ms)
{
    if (!s_queue_ctx.initialized) {
        return NULL;
    }

    SpotflowFrame *frame = NULL;
    osStatus status = osMutexWait(s_queue_ctx.pool_lock, adjust_timeout(timeout_ms));
    if (status == osOK) {
        if (s_queue_ctx.free_top > 0U) {
            frame = s_queue_ctx.free_stack[--s_queue_ctx.free_top];
        }
        (void)osMutexRelease(s_queue_ctx.pool_lock);
    }
    return frame;
}

void spotflow_queue_release_frame(SpotflowFrame *frame)
{
    if ((frame == NULL) || (!s_queue_ctx.initialized)) {
        return;
    }

    if (osMutexWait(s_queue_ctx.pool_lock, osWaitForever) == osOK) {
        if (s_queue_ctx.free_top < SPOTFLOW_QUEUE_DEPTH) {
            s_queue_ctx.free_stack[s_queue_ctx.free_top++] = frame;
        }
        (void)osMutexRelease(s_queue_ctx.pool_lock);
    }
}

bool spotflow_queue_submit(SpotflowFrame *frame, uint32_t timeout_ms)
{
    if ((!s_queue_ctx.initialized) || (frame == NULL)) {
        return false;
    }

    osStatus status =
        osMessagePut(s_queue_ctx.queue_id, (uint32_t)(uintptr_t)frame, adjust_timeout(timeout_ms));
    return (status == osOK);
}

SpotflowFrame *spotflow_queue_consume(uint32_t timeout_ms)
{
    if (!s_queue_ctx.initialized) {
        return NULL;
    }

    osEvent evt = osMessageGet(s_queue_ctx.queue_id, timeout_ms);
    if (evt.status == osEventMessage) {
        return (SpotflowFrame *)evt.value.p;
    }

    return NULL;
}
