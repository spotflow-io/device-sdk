#include "spotflow_cbor.h"
#include <stdbool.h>
#include <string.h>

typedef struct {
    uint8_t *buf;
    size_t capacity;
    size_t offset;
} SpotflowCborCtx;

static bool cbor_write_byte(SpotflowCborCtx *ctx, uint8_t value)
{
    if (ctx->offset >= ctx->capacity) {
        return false;
    }
    ctx->buf[ctx->offset++] = value;
    return true;
}

static bool cbor_write_type(SpotflowCborCtx *ctx, uint8_t major, uint32_t value)
{
    uint8_t additional;
    if (value < 24U) {
        additional = (uint8_t)value;
        return cbor_write_byte(ctx, (uint8_t)((major << 5) | additional));
    } else if (value <= UINT8_MAX) {
        if (!cbor_write_byte(ctx, (uint8_t)((major << 5) | 24U))) {
            return false;
        }
        return cbor_write_byte(ctx, (uint8_t)value);
    } else if (value <= UINT16_MAX) {
        if (!cbor_write_byte(ctx, (uint8_t)((major << 5) | 25U))) {
            return false;
        }
        if (!cbor_write_byte(ctx, (uint8_t)((value >> 8) & 0xFFU))) {
            return false;
        }
        return cbor_write_byte(ctx, (uint8_t)(value & 0xFFU));
    } else {
        if (!cbor_write_byte(ctx, (uint8_t)((major << 5) | 26U))) {
            return false;
        }
        for (int shift = 24; shift >= 0; shift -= 8) {
            if (!cbor_write_byte(ctx, (uint8_t)((value >> shift) & 0xFFU))) {
                return false;
            }
        }
        return true;
    }
}

static bool cbor_write_uint(SpotflowCborCtx *ctx, uint32_t value)
{
    return cbor_write_type(ctx, 0U, value);
}

static bool cbor_write_raw(SpotflowCborCtx *ctx, const uint8_t *data, size_t len)
{
    if ((ctx->capacity - ctx->offset) < len) {
        return false;
    }
    if (len > 0U) {
        memcpy(&ctx->buf[ctx->offset], data, len);
        ctx->offset += len;
    }
    return true;
}

static bool cbor_write_text(SpotflowCborCtx *ctx, const char *text)
{
    if (text == NULL) {
        text = "";
    }
    size_t len = strlen(text);
    if (!cbor_write_type(ctx, 3U, (uint32_t)len)) {
        return false;
    }
    return cbor_write_raw(ctx, (const uint8_t *)text, len);
}

size_t spotflow_cbor_encode_log(uint32_t timestamp_ms,
                                SpotflowLevel level,
                                const char *tag,
                                const char *message,
                                uint8_t *out,
                                size_t max_len)
{
    if ((out == NULL) || (max_len == 0U)) {
        return 0U;
    }

    SpotflowCborCtx ctx = {
        .buf = out,
        .capacity = max_len,
        .offset = 0U,
    };

    const char *key_ts = "ts";
    const char *key_lv = "lv";
    const char *key_tg = "tg";
    const char *key_ms = "ms";

    if (!cbor_write_type(&ctx, 5U, 4U)) { /* map with 4 pairs */
        return 0U;
    }

    if (!cbor_write_text(&ctx, key_ts) || !cbor_write_uint(&ctx, timestamp_ms)) {
        return 0U;
    }

    if (!cbor_write_text(&ctx, key_lv) ||
        !cbor_write_uint(&ctx, (uint32_t)level)) {
        return 0U;
    }

    if (!cbor_write_text(&ctx, key_tg) || !cbor_write_text(&ctx, tag)) {
        return 0U;
    }

    if (!cbor_write_text(&ctx, key_ms) || !cbor_write_text(&ctx, message)) {
        return 0U;
    }

    return ctx.offset;
}
