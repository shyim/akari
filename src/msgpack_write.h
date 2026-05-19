#ifndef MSGPACK_WRITE_H
#define MSGPACK_WRITE_H

/*
 * Minimal msgpack encoder — header-only, no external deps.
 * Supports: fixmap, map16, fixarray, array16, fixstr, str8, str16,
 *           bin8, bin16, uint8, uint16, uint32, uint64, fixint, nil.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} msgpack_buf_t;

static inline void msgpack_buf_init(msgpack_buf_t *buf, size_t initial_cap)
{
    buf->data = (uint8_t *)malloc(initial_cap);
    buf->len = 0;
    buf->cap = buf->data ? initial_cap : 0;
}

static inline void msgpack_buf_free(msgpack_buf_t *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->len = buf->cap = 0;
}

static inline int msgpack_buf_ensure(msgpack_buf_t *buf, size_t need)
{
    if (buf->len + need <= buf->cap) return 1;
    size_t new_cap = buf->cap * 2;
    if (new_cap < buf->len + need) new_cap = buf->len + need + 1024;
    uint8_t *p = (uint8_t *)realloc(buf->data, new_cap);
    if (!p) return 0;
    buf->data = p;
    buf->cap = new_cap;
    return 1;
}

static inline void msgpack_buf_write(msgpack_buf_t *buf, const void *src, size_t n)
{
    if (!msgpack_buf_ensure(buf, n)) return;
    memcpy(buf->data + buf->len, src, n);
    buf->len += n;
}

static inline void msgpack_buf_write_u8(msgpack_buf_t *buf, uint8_t v)
{
    if (!msgpack_buf_ensure(buf, 1)) return;
    buf->data[buf->len++] = v;
}

static inline void msgpack_buf_write_u16be(msgpack_buf_t *buf, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };
    msgpack_buf_write(buf, b, 2);
}

static inline void msgpack_buf_write_u32be(msgpack_buf_t *buf, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16),
                     (uint8_t)(v >> 8), (uint8_t)v };
    msgpack_buf_write(buf, b, 4);
}

static inline void msgpack_buf_write_u64be(msgpack_buf_t *buf, uint64_t v)
{
    uint8_t b[8] = {
        (uint8_t)(v >> 56), (uint8_t)(v >> 48),
        (uint8_t)(v >> 40), (uint8_t)(v >> 32),
        (uint8_t)(v >> 24), (uint8_t)(v >> 16),
        (uint8_t)(v >> 8),  (uint8_t)v
    };
    msgpack_buf_write(buf, b, 8);
}

/* ── msgpack type encoders ── */

static inline void msgpack_write_nil(msgpack_buf_t *buf)
{
    msgpack_buf_write_u8(buf, 0xc0);
}

static inline void msgpack_write_uint8(msgpack_buf_t *buf, uint8_t v)
{
    if (v <= 0x7f) {
        msgpack_buf_write_u8(buf, v); /* positive fixint */
    } else {
        msgpack_buf_write_u8(buf, 0xcc);
        msgpack_buf_write_u8(buf, v);
    }
}

static inline void msgpack_write_uint16(msgpack_buf_t *buf, uint16_t v)
{
    if (v <= 0xff) {
        msgpack_write_uint8(buf, (uint8_t)v);
    } else {
        msgpack_buf_write_u8(buf, 0xcd);
        msgpack_buf_write_u16be(buf, v);
    }
}

static inline void msgpack_write_uint32(msgpack_buf_t *buf, uint32_t v)
{
    if (v <= 0xffff) {
        msgpack_write_uint16(buf, (uint16_t)v);
    } else {
        msgpack_buf_write_u8(buf, 0xce);
        msgpack_buf_write_u32be(buf, v);
    }
}

static inline void msgpack_write_uint64(msgpack_buf_t *buf, uint64_t v)
{
    if (v <= 0xffffffff) {
        msgpack_write_uint32(buf, (uint32_t)v);
    } else {
        msgpack_buf_write_u8(buf, 0xcf);
        msgpack_buf_write_u64be(buf, v);
    }
}

static inline void msgpack_write_int(msgpack_buf_t *buf, int64_t v)
{
    if (v >= 0) {
        msgpack_write_uint64(buf, (uint64_t)v);
    } else if (v >= -32) {
        msgpack_buf_write_u8(buf, (uint8_t)(v & 0xff)); /* negative fixint */
    } else if (v >= -128) {
        msgpack_buf_write_u8(buf, 0xd0);
        msgpack_buf_write_u8(buf, (uint8_t)(int8_t)v);
    } else if (v >= -32768) {
        msgpack_buf_write_u8(buf, 0xd1);
        msgpack_buf_write_u16be(buf, (uint16_t)(int16_t)v);
    } else if (v >= -2147483648LL) {
        msgpack_buf_write_u8(buf, 0xd2);
        msgpack_buf_write_u32be(buf, (uint32_t)(int32_t)v);
    } else {
        msgpack_buf_write_u8(buf, 0xd3);
        msgpack_buf_write_u64be(buf, (uint64_t)v);
    }
}

static inline void msgpack_write_map(msgpack_buf_t *buf, uint32_t n)
{
    if (n <= 15) {
        msgpack_buf_write_u8(buf, 0x80 | (uint8_t)n); /* fixmap */
    } else {
        msgpack_buf_write_u8(buf, 0xde); /* map16 */
        msgpack_buf_write_u16be(buf, (uint16_t)n);
    }
}

static inline void msgpack_write_array(msgpack_buf_t *buf, uint32_t n)
{
    if (n <= 15) {
        msgpack_buf_write_u8(buf, 0x90 | (uint8_t)n); /* fixarray */
    } else {
        msgpack_buf_write_u8(buf, 0xdc); /* array16 */
        msgpack_buf_write_u16be(buf, (uint16_t)n);
    }
}

static inline void msgpack_write_str(msgpack_buf_t *buf, const char *s, size_t len)
{
    if (len <= 31) {
        msgpack_buf_write_u8(buf, 0xa0 | (uint8_t)len); /* fixstr */
    } else if (len <= 0xff) {
        msgpack_buf_write_u8(buf, 0xd9); /* str8 */
        msgpack_buf_write_u8(buf, (uint8_t)len);
    } else {
        msgpack_buf_write_u8(buf, 0xda); /* str16 */
        msgpack_buf_write_u16be(buf, (uint16_t)len);
    }
    msgpack_buf_write(buf, s, len);
}

static inline void msgpack_write_bin(msgpack_buf_t *buf, const void *data, size_t len)
{
    if (len <= 0xff) {
        msgpack_buf_write_u8(buf, 0xc4); /* bin8 */
        msgpack_buf_write_u8(buf, (uint8_t)len);
    } else {
        msgpack_buf_write_u8(buf, 0xc5); /* bin16 */
        msgpack_buf_write_u16be(buf, (uint16_t)len);
    }
    msgpack_buf_write(buf, data, len);
}

/* Convenience: write a string key (for map entries) */
static inline void msgpack_write_key(msgpack_buf_t *buf, const char *key)
{
    msgpack_write_str(buf, key, strlen(key));
}

#endif /* MSGPACK_WRITE_H */
