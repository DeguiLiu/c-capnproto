/* capn-malloc.c
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

#include "capnp_c.h"
#include "capnp_priv.h"
#include <limits.h>
#include <errno.h>

/*
 * 8 byte alignment is required for struct capn_segment.
 * This struct check_segment_alignment verifies this at compile time.
 *
 * Unless capn_segment is defined with 8 byte alignment, check_segment_alignment
 * fails to compile in x86 mode (or on another CPU with 32-bit pointers),
 * as (sizeof(struct capn_segment)&7) -> (44 & 7) evaluates to 4.
 * It compiles in x64 mode (or on another CPU with 64-bit pointers),
 * as (sizeof(struct capn_segment)&7) -> (80 & 7) evaluates to 0.
 */
struct check_segment_alignment
{
    rt_uint32_t foo : (sizeof(struct capn_segment) & 7) ? -1 : 1;
};

static struct capn_segment *create(void *u, rt_uint32_t id, rt_int32_t sz)
{
    struct capn_segment *s;
    (void)u;
    (void)id;

    sz += (rt_int32_t)sizeof(*s);
    if (sz < (rt_int32_t)CAPN_DEFAULT_SEG_SIZE)
    {
        sz = (rt_int32_t)CAPN_DEFAULT_SEG_SIZE;
    }
    else
    {
        sz = (sz + (rt_int32_t)(CAPN_DEFAULT_SEG_SIZE - 1)) & ~(rt_int32_t)(CAPN_DEFAULT_SEG_SIZE - 1);
    }

    s = (struct capn_segment *)rt_calloc(1, (rt_size_t)sz);
    if (CAPN_UNLIKELY(RT_NULL == s))
    {
        return RT_NULL;
    }

    s->data = (char *)(s + 1);
    s->cap = (rt_size_t)sz - sizeof(*s);
    s->user = s;
    return s;
}

static struct capn_segment *create_local(void *u, rt_int32_t sz)
{
    return create(u, 0, sz);
}

void capn_init_malloc(struct capn *c)
{
    rt_memset(c, 0, sizeof(*c));
    c->create = &create;
    c->create_local = &create_local;
}

void capn_free(struct capn *c)
{
    struct capn_segment *s = c->seglist;
    while (RT_NULL != s)
    {
        struct capn_segment *n = s->next;
        rt_free(s->user);
        s = n;
    }
    capn_reset_copy(c);
}

void capn_reset_copy(struct capn *c)
{
    struct capn_segment *s = c->copylist;
    while (RT_NULL != s)
    {
        struct capn_segment *n = s->next;
        rt_free(s->user);
        s = n;
    }
    c->copy = RT_NULL;
    c->copylist = RT_NULL;
}

#define ZBUF_SZ 4096

/* Stream-based read: works with memory buffers (via capn_stream) */
static rt_int32_t read_stream(void *p, rt_size_t sz, struct capn_stream *z, rt_int32_t packed)
{
    if (0 != packed)
    {
        z->next_out = (rt_uint8_t *)p;
        z->avail_out = sz;
        return (0 != capn_inflate(z)) ? CAPN_NEED_MORE : 0;
    }
    else
    {
        if (z->avail_in < sz)
        {
            return CAPN_NEED_MORE;
        }
        rt_memcpy(p, z->next_in, sz);
        z->next_in += sz;
        z->avail_in -= sz;
        return 0;
    }
}

#ifndef CAPN_NO_STDIO
/* FILE-based read: wraps fread with optional packed inflate */
static rt_int32_t read_fp(void *p, rt_size_t sz, FILE *f, struct capn_stream *z,
                          rt_uint8_t *zbuf, rt_int32_t packed)
{
    if (0 != packed)
    {
        z->next_out = (rt_uint8_t *)p;
        z->avail_out = sz;

        while ((0 != z->avail_out) && (CAPN_NEED_MORE == capn_inflate(z)))
        {
            rt_int32_t r;
            rt_memmove(zbuf, z->next_in, z->avail_in);
            r = (rt_int32_t)fread(zbuf + z->avail_in, 1, ZBUF_SZ - z->avail_in, f);
            if (r <= 0)
            {
                return CAPN_NEED_MORE;
            }
            z->avail_in += (rt_size_t)r;
        }
        return 0;
    }
    else
    {
        return (1 != fread(p, sz, 1, f)) ? CAPN_NEED_MORE : 0;
    }
}
#endif /* CAPN_NO_STDIO */

static rt_int32_t init_stream(struct capn *c, struct capn_stream *z, rt_int32_t packed)
{
    /*
     * Initialize 'c' from a capn_stream, assuming the message has been
     * serialized with the standard framing format.
     */

    struct capn_segment *s = RT_NULL;
    rt_uint32_t i;
    rt_uint32_t segnum;
    rt_uint32_t total = 0;
    rt_uint32_t *hdr = RT_NULL;
    char *data = RT_NULL;
    rt_size_t hdr_bytes;
    rt_int32_t result = CAPN_ERR_OVERFLOW;

    /* Read segment count first, then allocate header based on actual count */
    if (0 != read_stream(&segnum, 4, z, packed))
    {
        /* read failed, result already set to CAPN_ERR_OVERFLOW */
    }
    else
    {
        segnum = capn_flip32(segnum);
        if (segnum > CAPN_MAX_SEGMENTS - 1)
        {
            result = CAPN_ERR_SEG_OVERFLOW;
        }
        else
        {
            segnum++;  /* The wire encoding was zero-based */

            /* Allocate header sized for actual segment count, not max.
             * Wire format after segnum word: N segment sizes (4B each)
             * + optional 4B padding to reach 8-byte alignment.
             * Formula: (segnum / 2) * 8 + 4 */
            hdr_bytes = (segnum / 2) * CAPN_WORD_SIZE + sizeof(rt_uint32_t);
            hdr = (rt_uint32_t *)rt_malloc(hdr_bytes);
            if (RT_NULL == hdr)
            {
                result = CAPN_ERR_ALLOC_FAILED;
            }
            /* Read the header list (size matches hdr allocation) */
            else if (0 != read_stream(hdr, hdr_bytes, z, packed))
            {
                result = CAPN_ERR_OVERFLOW;
            }
            else
            {
                rt_int32_t hdr_valid = 1;
                for (i = 0; i < segnum; i++)
                {
                    rt_uint32_t n = capn_flip32(hdr[i]);
                    if ((n > INT_MAX / CAPN_WORD_SIZE) || (n > UINT32_MAX / CAPN_WORD_SIZE) || (UINT32_MAX - total < n * CAPN_WORD_SIZE))
                    {
                        hdr_valid = 0;
                        break;
                    }
                    hdr[i] = n * CAPN_WORD_SIZE;
                    total += hdr[i];
                }

                if (0 != hdr_valid)
                {
                    /* rt_malloc + partial memset: only zero the segment structs,
                     * not the data region which read_stream will overwrite */
                    s = (struct capn_segment *)rt_malloc(total + (sizeof(*s) * segnum));
                    if (RT_NULL != s)
                    {
                        rt_memset(s, 0, sizeof(*s) * segnum);
                        data = (char *)(s + segnum);
                        if (0 == read_stream(data, total, z, packed))
                        {
                            capn_init_malloc(c);

                            for (i = 0; i < segnum; i++)
                            {
                                s[i].len = s[i].cap = hdr[i];
                                s[i].data = data;
                                data += s[i].len;
                                capn_append_segment(c, &s[i]);
                            }

                            /* Set the entire region to be freed on the last segment */
                            s[segnum - 1].user = s;
                            result = 0;
                        }
                        else
                        {
                            result = CAPN_ERR_OVERFLOW;
                        }
                    }
                    else
                    {
                        result = CAPN_ERR_ALLOC_FAILED;
                    }
                }
                else
                {
                    result = CAPN_ERR_OVERFLOW;
                }
            }
        }
    }

    if (0 != result)
    {
        rt_memset(c, 0, sizeof(*c));
        if (RT_NULL != s)
        {
            rt_free(s);
        }
    }

    if (RT_NULL != hdr)
    {
        rt_free(hdr);
    }

    return result;
}

#ifndef CAPN_NO_STDIO
static rt_int32_t init_fp(struct capn *c, FILE *f, struct capn_stream *z, rt_int32_t packed)
{
    /*
     * Initialize 'c' from FILE*, using zbuf for packed inflate.
     */

    struct capn_segment *s = RT_NULL;
    rt_uint32_t i;
    rt_uint32_t segnum;
    rt_uint32_t total = 0;
    rt_uint32_t *hdr = RT_NULL;
    rt_uint8_t *zbuf = RT_NULL;
    char *data = RT_NULL;
    rt_size_t hdr_bytes;
    rt_int32_t result = CAPN_ERR_OVERFLOW;

    /* Defer zbuf allocation: only needed for packed mode */
    if (0 != packed)
    {
        zbuf = (rt_uint8_t *)rt_malloc(ZBUF_SZ);
        if (RT_NULL == zbuf)
        {
            result = CAPN_ERR_ALLOC_FAILED;
        }
    }

    /* Read segment count first, then allocate header based on actual count */
    if (CAPN_ERR_ALLOC_FAILED != result)
    {
        if (0 != read_fp(&segnum, 4, f, z, zbuf, packed))
        {
            /* read failed, result already set to CAPN_ERR_OVERFLOW */
        }
        else
        {
            segnum = capn_flip32(segnum);
            if (segnum > CAPN_MAX_SEGMENTS - 1)
            {
                result = CAPN_ERR_SEG_OVERFLOW;
            }
            else
            {
                segnum++;

                /* Allocate header sized for actual segment count, not max.
                 * Wire format after segnum word: N segment sizes (4B each)
                 * + optional 4B padding to reach 8-byte alignment.
                 * Formula: (segnum / 2) * 8 + 4 */
                hdr_bytes = (segnum / 2) * CAPN_WORD_SIZE + sizeof(rt_uint32_t);
                hdr = (rt_uint32_t *)rt_malloc(hdr_bytes);
                if (RT_NULL == hdr)
                {
                    result = CAPN_ERR_ALLOC_FAILED;
                }
                /* Read the header list (size matches hdr allocation) */
                else if (0 != read_fp(hdr, hdr_bytes, f, z, zbuf, packed))
                {
                    result = CAPN_ERR_OVERFLOW;
                }
                else
                {
                    rt_int32_t hdr_valid = 1;
                    for (i = 0; i < segnum; i++)
                    {
                        rt_uint32_t n = capn_flip32(hdr[i]);
                        if ((n > INT_MAX / CAPN_WORD_SIZE) || (n > UINT32_MAX / CAPN_WORD_SIZE) || (UINT32_MAX - total < n * CAPN_WORD_SIZE))
                        {
                            hdr_valid = 0;
                            break;
                        }
                        hdr[i] = n * CAPN_WORD_SIZE;
                        total += hdr[i];
                    }

                    if (0 != hdr_valid)
                    {
                        /* rt_malloc + partial memset: only zero the segment structs */
                        s = (struct capn_segment *)rt_malloc(total + (sizeof(*s) * segnum));
                        if (RT_NULL != s)
                        {
                            rt_memset(s, 0, sizeof(*s) * segnum);
                            data = (char *)(s + segnum);
                            if (0 == read_fp(data, total, f, z, zbuf, packed))
                            {
                                capn_init_malloc(c);

                                for (i = 0; i < segnum; i++)
                                {
                                    s[i].len = s[i].cap = hdr[i];
                                    s[i].data = data;
                                    data += s[i].len;
                                    capn_append_segment(c, &s[i]);
                                }

                                s[segnum - 1].user = s;
                                result = 0;
                            }
                            else
                            {
                                result = CAPN_ERR_OVERFLOW;
                            }
                        }
                        else
                        {
                            result = CAPN_ERR_ALLOC_FAILED;
                        }
                    }
                    else
                    {
                        result = CAPN_ERR_OVERFLOW;
                    }
                }
            }
        }
    }

    if (0 != result)
    {
        rt_memset(c, 0, sizeof(*c));
        if (RT_NULL != s)
        {
            rt_free(s);
        }
    }

    if (RT_NULL != hdr)
    {
        rt_free(hdr);
    }
    if (RT_NULL != zbuf)
    {
        rt_free(zbuf);
    }

    return result;
}

rt_int32_t capn_init_fp(struct capn *c, FILE *f, rt_int32_t packed)
{
    struct capn_stream z;
    rt_memset(&z, 0, sizeof(z));
    return init_fp(c, f, &z, packed);
}
#endif /* CAPN_NO_STDIO */

rt_int32_t capn_init_mem(struct capn *c, const rt_uint8_t *p, rt_size_t sz, rt_int32_t packed)
{
    struct capn_stream z;
    rt_memset(&z, 0, sizeof(z));
    z.next_in = p;
    z.avail_in = sz;
    return init_stream(c, &z, packed);
}

static void header_calc(struct capn *c, rt_uint32_t *headerlen, rt_size_t *headersz)
{
    /* segnum == 1:
     *   [segnum][segsiz]
     * segnum == 2:
     *   [segnum][segsiz][segsiz][zeroes]
     * segnum == 3:
     *   [segnum][segsiz][segsiz][segsiz]
     * segnum == 4:
     *   [segnum][segsiz][segsiz][segsiz][segsiz][zeroes]
     */
    *headerlen = ((2 + c->segnum) / 2) * 2;
    *headersz = 4 * *headerlen;
}

static rt_int32_t header_render(struct capn *c, struct capn_segment *seg,
                                rt_uint8_t *header, rt_uint32_t headerlen, rt_size_t *datasz)
{
    /* Write header as bytes to avoid alignment issues on strict platforms */
    rt_size_t i;
    rt_uint32_t val;

    val = capn_flip32(c->segnum - 1);
    rt_memcpy(header, &val, sizeof(val));

    /* Zero out the spare position in the header sizes */
    val = 0;
    rt_memcpy(header + (headerlen - 1) * sizeof(rt_uint32_t), &val, sizeof(val));

    for (i = 0; i < c->segnum; i++, seg = seg->next)
    {
        if (RT_NULL == seg)
        {
            return CAPN_ERR_SEG_OVERFLOW;
        }
        *datasz += seg->len;
        val = capn_flip32((rt_uint32_t)(seg->len / CAPN_WORD_SIZE));
        rt_memcpy(header + (1 + i) * sizeof(rt_uint32_t), &val, sizeof(val));
    }
    if (RT_NULL != seg)
    {
        return CAPN_ERR_SEG_OVERFLOW;
    }

    return 0;
}

static rt_int64_t capn_write_mem_packed(struct capn *c, rt_uint8_t *p, rt_size_t sz)
{
    struct capn_segment *seg;
    struct capn_ptr root;
    rt_uint32_t headerlen;
    rt_size_t headersz;
    rt_size_t datasz = 0;
    rt_uint8_t *header;
    struct capn_stream z;
    rt_int32_t ret;

    root = capn_root(c);
    header_calc(c, &headerlen, &headersz);
    /* No alignment requirement since header_render uses memcpy */
    header = p + headersz;

    if (sz < headersz * 2)  /* We must have space for temporary writing of header to deflate */
    {
        return CAPN_ERR_OVERFLOW;
    }

    ret = header_render(c, root.seg, header, headerlen, &datasz);
    if (0 != ret)
    {
        return (rt_int64_t)ret;
    }

    rt_memset(&z, 0, sizeof(z));
    z.next_in = header;
    z.avail_in = headersz;
    z.next_out = p;
    z.avail_out = sz;

    /* pack the headers */
    ret = capn_deflate(&z);
    if ((0 != ret) || (0 != z.avail_in))
    {
        return CAPN_NEED_MORE;
    }

    for (seg = root.seg; RT_NULL != seg; seg = seg->next)
    {
        z.next_in = (rt_uint8_t *)seg->data;
        z.avail_in = seg->len;
        ret = capn_deflate(&z);
        if ((0 != ret) || (0 != z.avail_in))
        {
            return CAPN_NEED_MORE;
        }
    }

    return (rt_int64_t)(sz - z.avail_out);
}

rt_int64_t capn_write_mem(struct capn *c, rt_uint8_t *p, rt_size_t sz, rt_int32_t packed)
{
    struct capn_segment *seg;
    struct capn_ptr root;
    rt_uint32_t headerlen;
    rt_size_t headersz;
    rt_size_t datasz = 0;
    rt_uint8_t *header;
    rt_int32_t ret;

    if (0 == c->segnum)
    {
        return CAPN_ERR_INVALID_PTR;
    }

    if (0 != packed)
    {
        return capn_write_mem_packed(c, p, sz);
    }

    root = capn_root(c);
    header_calc(c, &headerlen, &headersz);
    header = p;

    if (sz < headersz)
    {
        return CAPN_ERR_OVERFLOW;
    }

    ret = header_render(c, root.seg, header, headerlen, &datasz);
    if (0 != ret)
    {
        return (rt_int64_t)ret;
    }

    if (sz < headersz + datasz)
    {
        return CAPN_ERR_OVERFLOW;
    }

    p += headersz;

    for (seg = root.seg; RT_NULL != seg; seg = seg->next)
    {
        rt_memcpy(p, seg->data, seg->len);
        p += seg->len;
    }

    return (rt_int64_t)(headersz + datasz);
}

#ifndef CAPN_NO_STDIO
static rt_int32_t _write_fd(ssize_t (*write_fd)(int fd, const void *p, size_t count),
                            rt_int32_t fd, void *p, rt_size_t count)
{
    ssize_t ret;
    rt_size_t sent = 0;

    while (sent < count)
    {
        ret = write_fd((int)fd, ((rt_uint8_t *)p) + sent, count - sent);
        if (ret < 0)
        {
            if ((EAGAIN == errno) || (EINTR == errno))
            {
                continue;
            }
            else
            {
                return CAPN_NEED_MORE;
            }
        }
        sent += (rt_size_t)ret;
    }

    return 0;
}

#define CAPN_WRITE_BUF_SZ 4096

rt_int32_t capn_write_fd_with_buf(struct capn *c,
                                   ssize_t (*write_fd)(int fd, const void *p, size_t count),
                                   rt_int32_t fd, rt_int32_t packed,
                                   rt_uint8_t *buf, rt_size_t buf_sz)
{
    struct capn_segment *seg;
    struct capn_ptr root;
    rt_uint32_t headerlen;
    rt_size_t headersz;
    rt_size_t datasz = 0;
    rt_int32_t ret;
    struct capn_stream z;
    rt_uint8_t *p;

    if (0 == c->segnum)
    {
        return CAPN_ERR_INVALID_PTR;
    }

    root = capn_root(c);
    header_calc(c, &headerlen, &headersz);

    if (buf_sz < headersz)
    {
        return CAPN_ERR_OVERFLOW;
    }

    ret = header_render(c, root.seg, buf, headerlen, &datasz);
    if (0 != ret)
    {
        return ret;
    }

    if (0 != packed)
    {
        const rt_int32_t headerrem = (rt_int32_t)(buf_sz - headersz);
        const rt_int32_t maxpack = (rt_int32_t)(headersz + 2);
        if (headerrem < maxpack)
        {
            return CAPN_ERR_OVERFLOW;
        }

        rt_memset(&z, 0, sizeof(z));
        z.next_in = buf;
        z.avail_in = headersz;
        z.next_out = buf + headersz;
        z.avail_out = (rt_size_t)headerrem;
        ret = capn_deflate(&z);
        if (0 != ret)
        {
            return CAPN_NEED_MORE;
        }

        p = buf + headersz;
        headersz = (rt_size_t)headerrem - z.avail_out;
    }
    else
    {
        p = buf;
    }

    ret = _write_fd(write_fd, fd, p, headersz);
    if (ret < 0)
    {
        return ret;
    }

    datasz = headersz;
    for (seg = root.seg; RT_NULL != seg; seg = seg->next)
    {
        rt_size_t bufsz;
        if (0 != packed)
        {
            rt_memset(&z, 0, sizeof(z));
            z.next_in = (rt_uint8_t *)seg->data;
            z.avail_in = seg->len;
            z.next_out = buf;
            z.avail_out = buf_sz;
            ret = capn_deflate(&z);
            if (0 != ret)
            {
                return CAPN_NEED_MORE;
            }
            p = buf;
            bufsz = buf_sz - z.avail_out;
        }
        else
        {
            p = (rt_uint8_t *)seg->data;
            bufsz = seg->len;
        }
        ret = _write_fd(write_fd, fd, p, bufsz);
        if (ret < 0)
        {
            return ret;
        }
        datasz += bufsz;
    }

    return (rt_int32_t)datasz;
}

rt_int32_t capn_write_fd(struct capn *c,
                         ssize_t (*write_fd)(int fd, const void *p, size_t count),
                         rt_int32_t fd, rt_int32_t packed)
{
    rt_uint8_t *buf;
    rt_int32_t ret;

    buf = (rt_uint8_t *)rt_malloc(CAPN_WRITE_BUF_SZ);
    if (RT_NULL == buf)
    {
        return CAPN_ERR_ALLOC_FAILED;
    }

    ret = capn_write_fd_with_buf(c, write_fd, fd, packed, buf, CAPN_WRITE_BUF_SZ);
    rt_free(buf);
    return ret;
}
#endif /* CAPN_NO_STDIO */

rt_int64_t capn_size(struct capn *c)
{
    rt_size_t headersz;
    rt_size_t datasz = 0;
    struct capn_ptr root;
    struct capn_segment *seg;
    rt_uint32_t i;

    if (0 == c->segnum)
    {
        return CAPN_ERR_INVALID_PTR;
    }

    root = capn_root(c);
    seg = root.seg;

    headersz = CAPN_WORD_SIZE * ((2 + c->segnum) / 2);

    for (i = 0; i < c->segnum; i++, seg = seg->next)
    {
        if (RT_NULL == seg)
        {
            return CAPN_ERR_SEG_OVERFLOW;
        }
        datasz += seg->len;
    }
    if (RT_NULL != seg)
    {
        return CAPN_ERR_SEG_OVERFLOW;
    }

    return (rt_int64_t)(headersz + datasz);
}
