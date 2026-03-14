/* capnp_c.h
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

#ifndef CAPNP_C_H
#define CAPNP_C_H

#include "capnp_compat.h"
#include "capnp_constants.h"

#include <stdio.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#if defined(unix) && !defined(__APPLE__)
#include <endian.h>
#endif

/* Cross-platform macro ALIGNED_(x) aligns a struct by x bytes. */
#ifdef __GNUC__
#define ALIGNED_(x) __attribute__((aligned(x)))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)
    #if defined(__GNUC__) || defined(__clang__)
        #define CAPN_INLINE static __attribute__((always_inline)) inline
    #else
        #define CAPN_INLINE static inline
    #endif
#else
#define CAPN_INLINE static
#endif


/* capn_getp resolve flags */

/* Word size alias */
#define CAPN_WORD_SIZE          8      /* 8 bytes per word */

/* Branch prediction hints for hot paths */
#if defined(__GNUC__) || defined(__clang__)
#define CAPN_LIKELY(x)   __builtin_expect(!!(x), 1)
#define CAPN_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define CAPN_LIKELY(x)   (x)
#define CAPN_UNLIKELY(x) (x)
#endif

/* struct capn is a common structure shared between segments in the same
 * session/context so that far pointers between segments will be created.
 *
 * lookup is used to lookup segments by id when derefencing a far pointer.
 * create is used to create or lookup an alternate segment that has at least
 * sz available (ie returned seg->len + sz <= seg->cap).
 * create_local is used to create a segment for the copy tree and should be
 * allocated in the local memory space.
 *
 * Allocated segments must be zero initialized.
 *
 * create and lookup can be NULL if you don't need multiple segments and don't
 * want to support copying.
 *
 * seglist and copylist are linked lists which can be used to free up segments
 * on cleanup, but should not be modified by the user.
 *
 * lookup, create, create_local, and user can be set by the user. Other values
 * should be zero initialized.
 */
struct capn
{
    /* user settable */
    struct capn_segment *(*lookup)(void * /*user*/, rt_uint32_t /*id*/);
    struct capn_segment *(*create)(void * /*user*/, rt_uint32_t /*id*/, rt_int32_t /*sz*/);
    struct capn_segment *(*create_local)(void * /*user*/, rt_int32_t /*sz*/);
    void *user;
    /* zero initialized, user should not modify */
    rt_uint32_t segnum;
    struct capn_tree *copy;
    struct capn_tree *segtree;
    struct capn_segment *seglist;
    struct capn_segment *lastseg;
    struct capn_segment *copylist;
};

/* struct capn_tree is a rb tree header used internally for the segment id
 * lookup and copy tree.
 */
struct capn_tree
{
    struct capn_tree *parent;
    struct capn_tree *link[2];
    rt_uint32_t red : 1;
};

struct capn_tree *capn_tree_insert(struct capn_tree *root, struct capn_tree *n);

/* struct capn_segment contains the information about a single segment.
 *
 * capn points to a struct capn that is shared between segments in the
 * same session.
 *
 * id specifies the segment id, used for far pointers.
 *
 * data specifies the segment data. This should not move after creation.
 *
 * len specifies the current segment length. This is 0 for a blank segment.
 *
 * cap specifies the segment capacity.
 *
 * When creating new structures len will be incremented until it reaches cap,
 * at which point a new segment will be requested via capn->create. The
 * create callback can either create a new segment or expand an existing
 * one by incrementing cap and returning the expanded segment.
 *
 * data, len, and cap must all be 8 byte aligned.
 *
 * data, len, cap, and user should all be set by the user. Other values
 * should be zero initialized.
 */
struct ALIGNED_(8) capn_segment
{
    struct capn_tree hdr;
    struct capn_segment *next;
    struct capn *capn;
    rt_uint32_t id;
    /* user settable */
    char *data;
    rt_size_t len;
    rt_size_t cap;
    void *user;
};

enum CAPN_TYPE
{
    CAPN_NULL = 0,
    CAPN_STRUCT = 1,
    CAPN_LIST = 2,
    CAPN_PTR_LIST = 3,
    CAPN_BIT_LIST = 4,
    CAPN_FAR_POINTER = 5
};

struct capn_ptr
{
    rt_uint32_t type : 4;
    rt_uint32_t has_ptr_tag : 1;
    rt_uint32_t is_list_member : 1;
    rt_uint32_t is_composite_list : 1;
    rt_uint32_t datasz : 19;
    rt_uint32_t ptrs : 16;
    rt_int32_t len;
    char *data;
    struct capn_segment *seg;
};

struct capn_text
{
    rt_int32_t len;
    const char *str;
    struct capn_segment *seg;
};

typedef struct capn_ptr capn_ptr;
typedef struct capn_text capn_text;
typedef struct { capn_ptr p; } capn_data;
typedef struct { capn_ptr p; } capn_list1;
typedef struct { capn_ptr p; } capn_list8;
typedef struct { capn_ptr p; } capn_list16;
typedef struct { capn_ptr p; } capn_list32;
typedef struct { capn_ptr p; } capn_list64;

struct capn_msg
{
    struct capn_segment *seg;
    rt_uint64_t iface;
    rt_uint16_t method;
    capn_ptr args;
};

/* capn_append_segment appends a segment to a session. */
void capn_append_segment(struct capn *c, struct capn_segment *s);

capn_ptr capn_root(struct capn *c);
void capn_resolve(capn_ptr *p);

#define capn_len(list) \
    (CAPN_FAR_POINTER == (list).p.type ? (capn_resolve(&(list).p), (list).p.len) : (list).p.len)

/* capn_getp|setp functions get/set ptrs in list/structs.
 * off is the list index or pointer index in a struct.
 * capn_setp will copy the data, create far pointers, etc if the target
 * is in a different segment/context.
 * Both of these will use/return inner pointers for composite lists.
 */
capn_ptr capn_getp(capn_ptr p, rt_int32_t off, rt_int32_t resolve);
rt_int32_t capn_setp(capn_ptr p, rt_int32_t off, capn_ptr tgt);

capn_text capn_get_text(capn_ptr p, rt_int32_t off, capn_text def);
capn_data capn_get_data(capn_ptr p, rt_int32_t off);
rt_int32_t capn_set_text(capn_ptr p, rt_int32_t off, capn_text tgt);
/* There is no set_data -- use capn_new_list8 + capn_setv8 instead
 * and set data.p = list.p.
 */

/* capn_get* functions get data from a list.
 * The length of the list is given by p->size.
 * off specifies how far into the list to start.
 * sz indicates the number of elements to get.
 * The function returns the number of elements read or -1 on an error.
 * off must be byte aligned for capn_getv1.
 */
rt_int32_t capn_get1(capn_list1 p, rt_int32_t off);
rt_uint8_t capn_get8(capn_list8 p, rt_int32_t off);
rt_uint16_t capn_get16(capn_list16 p, rt_int32_t off);
rt_uint32_t capn_get32(capn_list32 p, rt_int32_t off);
rt_uint64_t capn_get64(capn_list64 p, rt_int32_t off);
rt_int32_t capn_getv1(capn_list1 p, rt_int32_t off, rt_uint8_t *data, rt_int32_t sz);
rt_int32_t capn_getv8(capn_list8 p, rt_int32_t off, rt_uint8_t *data, rt_int32_t sz);
rt_int32_t capn_getv16(capn_list16 p, rt_int32_t off, rt_uint16_t *data, rt_int32_t sz);
rt_int32_t capn_getv32(capn_list32 p, rt_int32_t off, rt_uint32_t *data, rt_int32_t sz);
rt_int32_t capn_getv64(capn_list64 p, rt_int32_t off, rt_uint64_t *data, rt_int32_t sz);

/* capn_set* functions set data in a list.
 * off specifies how far into the list to start.
 * sz indicates the number of elements to write.
 * The function returns the number of elements written or -1 on an error.
 * off must be byte aligned for capn_setv1.
 */
rt_int32_t capn_set1(capn_list1 p, rt_int32_t off, rt_int32_t v);
rt_int32_t capn_set8(capn_list8 p, rt_int32_t off, rt_uint8_t v);
rt_int32_t capn_set16(capn_list16 p, rt_int32_t off, rt_uint16_t v);
rt_int32_t capn_set32(capn_list32 p, rt_int32_t off, rt_uint32_t v);
rt_int32_t capn_set64(capn_list64 p, rt_int32_t off, rt_uint64_t v);
rt_int32_t capn_setv1(capn_list1 p, rt_int32_t off, const rt_uint8_t *data, rt_int32_t sz);
rt_int32_t capn_setv8(capn_list8 p, rt_int32_t off, const rt_uint8_t *data, rt_int32_t sz);
rt_int32_t capn_setv16(capn_list16 p, rt_int32_t off, const rt_uint16_t *data, rt_int32_t sz);
rt_int32_t capn_setv32(capn_list32 p, rt_int32_t off, const rt_uint32_t *data, rt_int32_t sz);
rt_int32_t capn_setv64(capn_list64 p, rt_int32_t off, const rt_uint64_t *data, rt_int32_t sz);

/* capn_new_* functions create a new object.
 * datasz is in bytes, ptrs is # of pointers, sz is # of elements in the list.
 * On an error a CAPN_NULL pointer is returned.
 */
capn_ptr capn_new_string(struct capn_segment *seg, const char *str, ssize_t sz);
capn_ptr capn_new_struct(struct capn_segment *seg, rt_int32_t datasz, rt_int32_t ptrs);
capn_ptr capn_new_interface(struct capn_segment *seg, rt_int32_t datasz, rt_int32_t ptrs);
capn_ptr capn_new_ptr_list(struct capn_segment *seg, rt_int32_t sz);
capn_ptr capn_new_list(struct capn_segment *seg, rt_int32_t sz, rt_int32_t datasz, rt_int32_t ptrs);
capn_list1 capn_new_list1(struct capn_segment *seg, rt_int32_t sz);
capn_list8 capn_new_list8(struct capn_segment *seg, rt_int32_t sz);
capn_list16 capn_new_list16(struct capn_segment *seg, rt_int32_t sz);
capn_list32 capn_new_list32(struct capn_segment *seg, rt_int32_t sz);
capn_list64 capn_new_list64(struct capn_segment *seg, rt_int32_t sz);

/* capn_read|write* functions read/write struct values.
 * off is the offset into the structure in bytes.
 * Rarely should these be called directly, instead use the generated code.
 * Data is stored as raw values (no XOR with default).
 * These are inlined.
 */
CAPN_INLINE rt_uint8_t capn_read8(capn_ptr p, rt_int32_t off);
CAPN_INLINE rt_uint16_t capn_read16(capn_ptr p, rt_int32_t off);
CAPN_INLINE rt_uint32_t capn_read32(capn_ptr p, rt_int32_t off);
CAPN_INLINE rt_uint64_t capn_read64(capn_ptr p, rt_int32_t off);
CAPN_INLINE rt_int32_t capn_write1(capn_ptr p, rt_int32_t off, rt_int32_t val);
CAPN_INLINE rt_int32_t capn_write8(capn_ptr p, rt_int32_t off, rt_uint8_t val);
CAPN_INLINE rt_int32_t capn_write16(capn_ptr p, rt_int32_t off, rt_uint16_t val);
CAPN_INLINE rt_int32_t capn_write32(capn_ptr p, rt_int32_t off, rt_uint32_t val);
CAPN_INLINE rt_int32_t capn_write64(capn_ptr p, rt_int32_t off, rt_uint64_t val);

/* capn_init_malloc inits the capn struct with a create function which
 * allocates segments on the heap using rt_malloc.
 *
 * capn_init_(fp|mem) inits by reading segments in from the file/memory buffer
 * in serialized form (optionally packed). It will then setup the create
 * function ala capn_init_malloc so that further segments can be created.
 *
 * capn_free frees all the segment headers and data created by the create
 * function setup by capn_init_*.
 */
void capn_init_malloc(struct capn *c);
rt_int32_t capn_init_mem(struct capn *c, const rt_uint8_t *p, rt_size_t sz, rt_int32_t packed);

/* FILE*-based API: only available on host platforms with stdio.
 * Define CAPN_NO_STDIO to exclude (e.g. for RT-Thread embedded targets). */
#ifndef CAPN_NO_STDIO
rt_int32_t capn_init_fp(struct capn *c, FILE *f, rt_int32_t packed);
#endif

/* capn_size() calculates the amount of memory required to serialise the given
 * Cap'n Proto structure in the unpacked format. It does NOT apply to packed
 * serialisation, as that may (in rare cases) actually become bigger than the
 * input. A buffer of this size can then be passed to capn_write_mem() without
 * fear of truncation (again, only in the unpacked case).
 */
rt_int64_t capn_size(struct capn *c);

/* capn_write_(fd|mem) writes segments to the fd/memory buffer in
 * serialized form and returns the number of bytes written.
 */
/* fd-based write API: uses caller-provided write callback.
 * Define CAPN_NO_STDIO to exclude. */
#ifndef CAPN_NO_STDIO
rt_int32_t capn_write_fd(struct capn *c,
                         ssize_t (*write_fn)(int fd, const void *p, size_t count),
                         rt_int32_t fd, rt_int32_t packed);
/* Zero-allocation variant: caller provides work buffer (min CAPN_MIN_WORK_BUFFER_SIZE_U bytes). */
rt_int32_t capn_write_fd_with_buf(struct capn *c,
                                   ssize_t (*write_fn)(int fd, const void *p, size_t count),
                                   rt_int32_t fd, rt_int32_t packed,
                                   rt_uint8_t *work_buf, rt_size_t buf_sz);
#endif
rt_int64_t capn_write_mem(struct capn *c, rt_uint8_t *p, rt_size_t sz, rt_int32_t packed);

void capn_free(struct capn *c);
void capn_reset_copy(struct capn *c);

/* ========================================================================== */
/* Inline functions                                                           */
/* ========================================================================== */

CAPN_INLINE rt_uint8_t capn_flip8(rt_uint8_t v)
{
    return v;
}

CAPN_INLINE rt_uint16_t capn_flip16(rt_uint16_t v)
{
#if (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)) \
    || (defined(__BYTE_ORDER) && (__BYTE_ORDER == __LITTLE_ENDIAN))
    return v;
#elif (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)) \
      || (defined(__BYTE_ORDER) && (__BYTE_ORDER == __BIG_ENDIAN))
    return __builtin_bswap16(v);
#else
    union { rt_uint16_t u; rt_uint8_t b[2]; } s;
    s.b[0] = (rt_uint8_t)v;
    s.b[1] = (rt_uint8_t)(v >> 8);
    return s.u;
#endif
}

CAPN_INLINE rt_uint32_t capn_flip32(rt_uint32_t v)
{
#if (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)) \
    || (defined(__BYTE_ORDER) && (__BYTE_ORDER == __LITTLE_ENDIAN))
    return v;
#elif (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)) \
      || (defined(__BYTE_ORDER) && (__BYTE_ORDER == __BIG_ENDIAN))
    return __builtin_bswap32(v);
#else
    union { rt_uint32_t u; rt_uint8_t b[4]; } s;
    s.b[0] = (rt_uint8_t)v;
    s.b[1] = (rt_uint8_t)(v >> 8);
    s.b[2] = (rt_uint8_t)(v >> 16);
    s.b[3] = (rt_uint8_t)(v >> 24);
    return s.u;
#endif
}

CAPN_INLINE rt_uint64_t capn_flip64(rt_uint64_t v)
{
#if (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)) \
    || (defined(__BYTE_ORDER) && (__BYTE_ORDER == __LITTLE_ENDIAN))
    return v;
#elif (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)) \
      || (defined(__BYTE_ORDER) && (__BYTE_ORDER == __BIG_ENDIAN))
    return __builtin_bswap64(v);
#else
    union { rt_uint64_t u; rt_uint8_t b[8]; } s;
    s.b[0] = (rt_uint8_t)v;
    s.b[1] = (rt_uint8_t)(v >> 8);
    s.b[2] = (rt_uint8_t)(v >> 16);
    s.b[3] = (rt_uint8_t)(v >> 24);
    s.b[4] = (rt_uint8_t)(v >> 32);
    s.b[5] = (rt_uint8_t)(v >> 40);
    s.b[6] = (rt_uint8_t)(v >> 48);
    s.b[7] = (rt_uint8_t)(v >> 56);
    return s.u;
#endif
}

CAPN_INLINE rt_int32_t capn_write1(capn_ptr p, rt_int32_t off, rt_int32_t val)
{
    if (CAPN_UNLIKELY(off >= (rt_int32_t)p.datasz * 8))
    {
        return -1;
    }
    else if (0 != val)
    {
        rt_uint8_t tmp = (rt_uint8_t)(1 << (off & 7));
        ((rt_uint8_t *)p.data)[off >> 3] |= tmp;
        return 0;
    }
    else
    {
        rt_uint8_t tmp = (rt_uint8_t)(~(1 << (off & 7)));
        ((rt_uint8_t *)p.data)[off >> 3] &= tmp;
        return 0;
    }
}

CAPN_INLINE rt_uint8_t capn_read8(capn_ptr p, rt_int32_t off)
{
    if (CAPN_LIKELY(off + 1 <= (rt_int32_t)p.datasz))
    {
        return capn_flip8(*(rt_uint8_t *)(p.data + off));
    }
    return 0;
}

CAPN_INLINE rt_int32_t capn_write8(capn_ptr p, rt_int32_t off, rt_uint8_t val)
{
    if (CAPN_LIKELY(off + 1 <= (rt_int32_t)p.datasz))
    {
        *(rt_uint8_t *)(p.data + off) = capn_flip8(val);
        return 0;
    }
    else
    {
        return -1;
    }
}

CAPN_INLINE rt_uint16_t capn_read16(capn_ptr p, rt_int32_t off)
{
    if (CAPN_LIKELY(off + 2 <= (rt_int32_t)p.datasz))
    {
        return capn_flip16(*(rt_uint16_t *)(p.data + off));
    }
    return 0;
}

CAPN_INLINE rt_int32_t capn_write16(capn_ptr p, rt_int32_t off, rt_uint16_t val)
{
    if (CAPN_LIKELY(off + 2 <= (rt_int32_t)p.datasz))
    {
        *(rt_uint16_t *)(p.data + off) = capn_flip16(val);
        return 0;
    }
    else
    {
        return -1;
    }
}

CAPN_INLINE rt_uint32_t capn_read32(capn_ptr p, rt_int32_t off)
{
    if (CAPN_LIKELY(off + 4 <= (rt_int32_t)p.datasz))
    {
        return capn_flip32(*(rt_uint32_t *)(p.data + off));
    }
    return 0;
}

CAPN_INLINE rt_int32_t capn_write32(capn_ptr p, rt_int32_t off, rt_uint32_t val)
{
    if (CAPN_LIKELY(off + 4 <= (rt_int32_t)p.datasz))
    {
        *(rt_uint32_t *)(p.data + off) = capn_flip32(val);
        return 0;
    }
    else
    {
        return -1;
    }
}

CAPN_INLINE rt_uint64_t capn_read64(capn_ptr p, rt_int32_t off)
{
    if (CAPN_LIKELY(off + 8 <= (rt_int32_t)p.datasz))
    {
        return capn_flip64(*(rt_uint64_t *)(p.data + off));
    }
    return 0;
}

CAPN_INLINE rt_int32_t capn_write64(capn_ptr p, rt_int32_t off, rt_uint64_t val)
{
    if (CAPN_LIKELY(off + 8 <= (rt_int32_t)p.datasz))
    {
        *(rt_uint64_t *)(p.data + off) = capn_flip64(val);
        return 0;
    }
    else
    {
        return -1;
    }
}

union capn_conv_f32
{
    rt_uint32_t u;
    float f;
};

union capn_conv_f64
{
    rt_uint64_t u;
    double f;
};

CAPN_INLINE float capn_to_f32(rt_uint32_t v)
{
    union capn_conv_f32 u;
    u.u = v;
    return u.f;
}

CAPN_INLINE double capn_to_f64(rt_uint64_t v)
{
    union capn_conv_f64 u;
    u.u = v;
    return u.f;
}

CAPN_INLINE rt_uint32_t capn_from_f32(float v)
{
    union capn_conv_f32 u;
    u.f = v;
    return u.u;
}

CAPN_INLINE rt_uint64_t capn_from_f64(double v)
{
    union capn_conv_f64 u;
    u.f = v;
    return u.u;
}

/* ========================================================================== */
/* Unchecked read variants (hot path, for generated code)                     */
/* ========================================================================== */

/*
 * These variants skip bounds checking for performance.
 * Caller MUST guarantee: off + N <= p.datasz (where N = 1/2/4/8).
 * Use only in generated read_*() functions where offset is schema-determined.
 */
CAPN_INLINE rt_uint8_t capn_read8_unchecked(capn_ptr p, rt_int32_t off)
{
    return *(rt_uint8_t *)(p.data + off);
}

CAPN_INLINE rt_uint16_t capn_read16_unchecked(capn_ptr p, rt_int32_t off)
{
    return capn_flip16(*(rt_uint16_t *)(p.data + off));
}

CAPN_INLINE rt_uint32_t capn_read32_unchecked(capn_ptr p, rt_int32_t off)
{
    return capn_flip32(*(rt_uint32_t *)(p.data + off));
}

CAPN_INLINE rt_uint64_t capn_read64_unchecked(capn_ptr p, rt_int32_t off)
{
    return capn_flip64(*(rt_uint64_t *)(p.data + off));
}

#ifdef __cplusplus
}
#endif

#endif /* CAPNP_C_H */
