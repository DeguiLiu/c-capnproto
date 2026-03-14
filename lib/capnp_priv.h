/* capnp_priv.h
 *
 * Copyright (C) 2013 James McKaskill
 * Copyright (C) 2014 Steve Dee
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 *
 * Modified by Raysen Microsystem Technology Co., Ltd. 2025-2026.
 * RT-Thread/MISRA C:2012 adaptation for RS500 platform.
 */

/*
 * Functions / structures in this header are private to the capnproto-c
 * library; applications should not call or use them.
 */

#ifndef CAPNP_PRIV_H
#define CAPNP_PRIV_H

#include "capnp_c.h"

#if defined(__GNUC__) && __GNUC__ >= 4
#define intern __attribute__((visibility("internal")))
#else
#define intern
#endif

/* capn_stream encapsulates the needed fields for capn_(deflate|inflate) in a
 * similar manner to z_stream from zlib.
 *
 * The user should set next_in, avail_in, next_out, avail_out to the
 * available in/out buffers before calling capn_(deflate|inflate).
 *
 * Other fields should be zero initialized.
 */
struct capn_stream
{
    const rt_uint8_t *next_in;
    rt_size_t avail_in;
    rt_uint8_t *next_out;
    rt_size_t avail_out;
    rt_uint32_t zeros;
    rt_uint32_t raw;

    rt_uint8_t inflate_buf[CAPN_WORD_SIZE];
    rt_size_t avail_buf;
};

/* Packed format constants */
#define CAPN_PACKED_TAG_ALL_ZERO     0x00U   /* All 8 bytes in word are zero */
#define CAPN_PACKED_TAG_ALL_NONZERO  0xFFU   /* All 8 bytes in word are non-zero */
#define CAPN_PACKED_HDR_SIZE         10U     /* 1 tag + 8 data + 1 raw count */
#define CAPN_MAX_PACKED_WORDS        256U    /* Max consecutive zero/raw words */

/* Wire pointer tag/kind masks */
#define CAPN_PTR_TAG_MASK   7U   /* Lower 3 bits: FAR_PTR / DOUBLE_PTR */
#define CAPN_PTR_KIND_MASK  3U   /* Lower 2 bits: STRUCT_PTR / LIST_PTR */

/* Segment allocation constants */
#define CAPN_DEFAULT_SEG_SIZE   4096U   /* Default segment allocation size */
#define CAPN_MAX_SEGMENTS       1024U   /* Maximum number of segments (wire: 0..1023) */

/* Error codes (-1 ~ -10, does not conflict with DM_ERR_xxx -100~-169) */
#define CAPN_MISALIGNED         (-1)  /* stream data not 8-byte aligned */
#define CAPN_NEED_MORE          (-2)  /* more data/room required */
#define CAPN_ERR_OVERFLOW       (-3)  /* offset/length out of bounds */
#define CAPN_ERR_INVALID_TYPE   (-4)  /* unexpected pointer/list type */
#define CAPN_ERR_ALLOC_FAILED   (-5)  /* memory allocation failed */
#define CAPN_ERR_DEPTH_EXCEEDED (-6)  /* copy depth > CAPN_MAX_COPY_DEPTH */
#define CAPN_ERR_INVALID_PTR    (-7)  /* null or corrupt pointer */
#define CAPN_ERR_SEG_OVERFLOW   (-8)  /* too many segments or chain broken */

/* capn_deflate deflates a stream to the packed format.
 * capn_inflate inflates a stream from the packed format.
 *
 * Returns:
 * CAPN_MISALIGNED - if the unpacked data is not 8 byte aligned
 * CAPN_NEED_MORE  - more packed data/room is required
 *                   (out for inflate, in for deflate)
 * 0               - success
 */
intern rt_int32_t capn_deflate(struct capn_stream *s);
intern rt_int32_t capn_inflate(struct capn_stream *s);

#endif /* CAPNP_PRIV_H */
