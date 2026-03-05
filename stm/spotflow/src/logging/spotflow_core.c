#include "spotflow.h"
#include "spotflow_config.h"
#include "spotflow_port.h"
#include "cbor/spotflow_cbor.h"
#include "queue/spotflow_queue.h"
#include "tasks/spotflow_tasks.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static bool s_initialized = false;
static SpotflowLevel s_min_level = (SpotflowLevel)SPOTFLOW_DEFAULT_LEVEL_VALUE;
static volatile uint32_t s_dropped = 0;
static volatile uint32_t s_sent = 0;

static bool spotflow_is_level_enabled(SpotflowLevel level)
{
    return level <= s_min_level;
}

bool Spotflow_init(void)
{
    if (s_initialized) {
        return true;
    }

    if (!spotflow_queue_init()) {
        return false;
    }

    if (!spotflow_tasks_start()) {
        return false;
    }

    s_initialized = true;
    return true;
}

void Spotflow_set_level(SpotflowLevel min_level)
{
    if (min_level > SPOTFLOW_LEVEL_DEBUG) {
        min_level = SPOTFLOW_LEVEL_DEBUG;
    }
    s_min_level = min_level;
}

static void spotflow_trim_tag(const char *tag_in, char *tag_out)
{
    if (!tag_in) {
        tag_out[0] = '\0';
        return;
    }

    size_t len = strlen(tag_in);
    if (len > SPOTFLOW_MAX_TAG_LEN) {
        len = SPOTFLOW_MAX_TAG_LEN;
    }
    memcpy(tag_out, tag_in, len);
    tag_out[len] = '\0';
}

bool Spotflow_log(SpotflowLevel level, const char *tag, const char *fmt, ...)
{
    if (!s_initialized) {
        spotflow_metrics_record_drop();
        return false;
    }

    if (!spotflow_is_level_enabled(level)) {
        return true;
    }

    char msg_buf[SPOTFLOW_MAX_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);
    msg_buf[SPOTFLOW_MAX_MSG_LEN - 1U] = '\0';

    char tag_buf[SPOTFLOW_MAX_TAG_LEN + 1U];
    spotflow_trim_tag(tag, tag_buf);

    SpotflowFrame *frame = spotflow_queue_acquire_frame(SPOTFLOW_LOG_API_TIMEOUT_MS);
    if (!frame) {
        spotflow_metrics_record_drop();
        return false;
    }

    uint32_t timestamp = spotflow_port_get_time_ms();
    size_t encoded = spotflow_cbor_encode_log(timestamp, level, tag_buf, msg_buf,
                                              frame->payload, sizeof(frame->payload));
    if ((encoded == 0U) || (encoded > UINT16_MAX)) {
        spotflow_queue_release_frame(frame);
        spotflow_metrics_record_drop();
        return false;
    }

    frame->len = (uint16_t)encoded;

    if (!spotflow_queue_submit(frame, SPOTFLOW_LOG_API_TIMEOUT_MS)) {
        spotflow_queue_release_frame(frame);
        spotflow_metrics_record_drop();
        return false;
    }

    return true;
}

// Exposes how many frames have been dropped so far for diagnostics.
uint32_t Spotflow_get_dropped(void)
{
    return s_dropped;
}

// Exposes the number of frames successfully handed to the transport layer.
uint32_t Spotflow_get_sent(void)
{
    return s_sent;
}

void spotflow_metrics_record_drop(void)
{
    SpotflowCriticalState state = spotflow_port_enter_critical();
    s_dropped++;
    spotflow_port_exit_critical(state);
}

void spotflow_metrics_record_send(void)
{
    SpotflowCriticalState state = spotflow_port_enter_critical();
    s_sent++;
    spotflow_port_exit_critical(state);
}
