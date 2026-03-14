/* capn.c
 *
 * Copyright (C) 2013 James McKaskill
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 *
 * Modified by Raysen Microsystem Technology Co., Ltd. 2025-2026.
 * RT-Thread/MISRA C:2012 adaptation for RS500 platform.
 */

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include "capnp_c.h"
#include "capnp_priv.h"

#define STRUCT_PTR 0
#define LIST_PTR 1
#define FAR_PTR 2
#define DOUBLE_PTR 6

#define VOID_LIST 0
#define BIT_1_LIST 1
#define BYTE_1_LIST 2
#define BYTE_2_LIST 3
#define BYTE_4_LIST 4
#define BYTE_8_LIST 5
#define PTR_LIST 6
#define COMPOSITE_LIST 7

#define CAPN_MIN(a, b) (((a) < (b)) ? (a) : (b))

#ifdef BYTE_ORDER
#define CAPN_LITTLE (BYTE_ORDER == LITTLE_ENDIAN)
#elif defined(__BYTE_ORDER)
#define CAPN_LITTLE (__BYTE_ORDER == __LITTLE_ENDIAN)
#else
#define CAPN_LITTLE 0
#endif

struct capn_tree *capn_tree_insert(struct capn_tree *root, struct capn_tree *n)
{
    n->red = 1;
    n->link[0] = RT_NULL;
    n->link[1] = RT_NULL;

    for (;;)
    {
        /* parent, uncle, grandparent, great grandparent link */
        struct capn_tree *p, *u, *g, **gglink;
        rt_int32_t dir;

        /* Case 1: N is root */
        p = n->parent;
        if (RT_NULL == p)
        {
            n->red = 0;
            root = n;
            break;
        }

        /* Case 2: p is black */
        if (0 == p->red)
        {
            break;
        }

        g = p->parent;
        dir = (p == g->link[1]);

        /* Case 3: P and U are red, switch g to red, but must
         * loop as G could be root or have a red parent
         *     g    to   G
         *    / \       / \
         *   P   U     p   u
         *  /         /
         * N         N
         */
        u = g->link[0 == dir ? 1 : 0];
        if (RT_NULL != u && u->red)
        {
            p->red = 0;
            u->red = 0;
            g->red = 1;
            n = g;
            continue;
        }

        if (RT_NULL == g->parent)
        {
            gglink = &root;
        }
        else if (g->parent->link[1] == g)
        {
            gglink = &g->parent->link[1];
        }
        else
        {
            gglink = &g->parent->link[0];
        }

        if (dir != (n == p->link[1]))
        {
            /* Case 4: rotate on P, then on g
             * here dir is /
             *     g    to   g   to   n
             *    / \       / \      / \
             *   P   u     N   u    P   G
             *  / \       / \      /|  / \
             * 1   N     P   3    1 2 3   u
             *    / \   / \
             *   2   3 1   2
             */
            struct capn_tree *two = n->link[dir];
            struct capn_tree *three = n->link[0 == dir ? 1 : 0];
            p->link[0 == dir ? 1 : 0] = two;
            g->link[dir] = three;
            n->link[dir] = p;
            n->link[0 == dir ? 1 : 0] = g;
            *gglink = n;
            n->parent = g->parent;
            p->parent = n;
            g->parent = n;
            if (RT_NULL != two)
            {
                two->parent = p;
            }
            if (RT_NULL != three)
            {
                three->parent = g;
            }
            n->red = 0;
            g->red = 1;
        }
        else
        {
            /* Case 5: rotate on g
             * here dir is /
             *       g   to   p
             *      / \      / \
             *     P   u    N   G
             *    / \      /|  / \
             *   N   3    1 2 3   u
             *  / \
             * 1   2
             */
            struct capn_tree *three = p->link[0 == dir ? 1 : 0];
            g->link[dir] = three;
            p->link[0 == dir ? 1 : 0] = g;
            *gglink = p;
            p->parent = g->parent;
            g->parent = p;
            if (RT_NULL != three)
            {
                three->parent = g;
            }
            g->red = 1;
            p->red = 0;
        }

        break;
    }

    return root;
}

void capn_append_segment(struct capn *c, struct capn_segment *s)
{
    s->id = c->segnum++;
    s->capn = c;
    s->next = RT_NULL;

    if (RT_NULL != c->lastseg)
    {
        c->lastseg->next = s;
        c->lastseg->hdr.link[1] = &s->hdr;
        s->hdr.parent = &c->lastseg->hdr;
    }
    else
    {
        c->seglist = s;
        s->hdr.parent = RT_NULL;
    }

    c->lastseg = s;
    c->segtree = capn_tree_insert(c->segtree, &s->hdr);
}

static char *new_data(struct capn *c, rt_int32_t sz, struct capn_segment **ps)
{
    struct capn_segment *s;
    rt_int32_t found = 0;

    /* find a segment with sufficient data */
    for (s = c->seglist; RT_NULL != s; s = s->next)
    {
        if (s->len + sz <= s->cap)
        {
            found = 1;
            break;
        }
    }

    if (0 == found)
    {
        s = (RT_NULL != c->create) ? c->create(c->user, c->segnum, sz) : RT_NULL;
        if (RT_NULL == s)
        {
            *ps = RT_NULL;
            return RT_NULL;
        }

        capn_append_segment(c, s);
    }

    *ps = s;
    s->len += sz;
    return s->data + s->len - sz;
}

static struct capn_segment *lookup_segment(struct capn *c, struct capn_segment *s, rt_uint32_t id)
{
    struct capn_tree **x;
    struct capn_segment *y = RT_NULL;

    if (RT_NULL != s && s->id == id)
    {
        return s;
    }
    if (RT_NULL == c)
    {
        return RT_NULL;
    }

    if (id < c->segnum)
    {
        x = &c->segtree;
        while (RT_NULL != *x)
        {
            y = (struct capn_segment *) *x;
            if (id == y->id)
            {
                return y;
            }
            else if (id < y->id)
            {
                x = &y->hdr.link[0];
            }
            else
            {
                x = &y->hdr.link[1];
            }
        }
    }
    else
    {
        /* Otherwise `x` may be uninitialized */
        return RT_NULL;
    }

    s = (RT_NULL != c->lookup) ? c->lookup(c->user, id) : RT_NULL;
    if (RT_NULL == s)
    {
        return RT_NULL;
    }

    if (id < c->segnum)
    {
        s->id = id;
        s->capn = c;
        s->next = c->seglist;
        c->seglist = s;
        s->hdr.parent = &y->hdr;
        *x = &s->hdr;
        c->segtree = capn_tree_insert(c->segtree, &s->hdr);
    }
    else
    {
        c->segnum = id;
        capn_append_segment(c, s);
    }

    return s;
}

static rt_uint64_t lookup_double(struct capn_segment **s, char **d, rt_uint64_t val)
{
    rt_uint64_t far, tag;
    rt_size_t off = ((rt_uint32_t)(val) >> CAPN_BIT_SHIFT_3) * CAPN_SIZE_UINT64;
    char *p;

    if (RT_NULL == (*s = lookup_segment((*s)->capn, *s, (rt_uint32_t)(val >> 32))))
    {
        return 0;
    }

    p = (*s)->data + off;
    if (off + (2 * CAPN_SIZE_UINT64) > (*s)->len)
    {
        return 0;
    }

    far = capn_flip64(*(rt_uint64_t *) p);
    tag = capn_flip64(*(rt_uint64_t *) (p + CAPN_SIZE_UINT64));

    /* the far tag should not be another double, and the tag
     * should be struct/list and have no offset */
    if (FAR_PTR != (far & CAPN_PTR_TAG_MASK) || (rt_uint32_t)(tag) > LIST_PTR)
    {
        return 0;
    }

    if (RT_NULL == (*s = lookup_segment((*s)->capn, *s, (rt_uint32_t)(far >> 32))))
    {
        return 0;
    }

    /* -8 because far pointers reference from the start of
     * the segment, but offsets reference the end of the
     * pointer data. Here *d points to where an equivalent
     * ptr would be.
     */
    *d = (*s)->data - CAPN_SIZE_UINT64;
    return (rt_uint64_t)((rt_uint32_t)(far) >> CAPN_BIT_SHIFT_3 << 2) | tag;
}

static rt_uint64_t lookup_far(struct capn_segment **s, char **d, rt_uint64_t val)
{
    rt_size_t off = ((rt_uint32_t)(val) >> CAPN_BIT_SHIFT_3) * CAPN_SIZE_UINT64;

    if (RT_NULL == (*s = lookup_segment((*s)->capn, *s, (rt_uint32_t)(val >> 32))))
    {
        return 0;
    }

    if (off + CAPN_SIZE_UINT64 > (*s)->len)
    {
        return 0;
    }

    *d = (*s)->data + off;
    return capn_flip64(*(rt_uint64_t *) *d);
}

static char *struct_ptr(struct capn_segment *s, char *d, rt_int32_t minsz)
{
    rt_uint64_t val = capn_flip64(*(rt_uint64_t *) d);
    rt_uint16_t datasz;

    switch (val & CAPN_PTR_TAG_MASK)
    {
    case FAR_PTR:
        val = lookup_far(&s, &d, val);
        break;
    case DOUBLE_PTR:
        val = lookup_double(&s, &d, val);
        break;
    default:
        break;
    }

    datasz = (rt_uint16_t)(val >> 32);
    d += ((rt_int32_t)((rt_uint32_t)(val)) << 1) + CAPN_SIZE_UINT64;

    if (0 != val && STRUCT_PTR == (val & CAPN_PTR_KIND_MASK) && datasz >= (rt_uint16_t)minsz
        && s->data <= d && d < s->data + s->len)
    {
        return d;
    }

    return RT_NULL;
}

static capn_ptr read_ptr(struct capn_segment *s, char *d)
{
    capn_ptr ret = {CAPN_NULL};
    rt_uint64_t val;
    char *e = RT_NULL;
    rt_int32_t read_ok = 1;

    val = capn_flip64(*(rt_uint64_t *) d);

    switch (val & CAPN_PTR_TAG_MASK)
    {
    case FAR_PTR:
        val = lookup_far(&s, &d, val);
        ret.has_ptr_tag = (0 == ((rt_uint32_t)(val) >> 2));
        break;
    case DOUBLE_PTR:
        val = lookup_double(&s, &d, val);
        break;
    default:
        break;
    }

    d += ((rt_int32_t)((rt_uint32_t)(val)) >> 2) * CAPN_SIZE_UINT64 + CAPN_SIZE_UINT64;

    if (d < s->data)
    {
        read_ok = 0;
    }

    if (0 != read_ok)
    {
        switch (val & CAPN_PTR_KIND_MASK)
        {
        case STRUCT_PTR:
            ret.type = (0 != val) ? CAPN_STRUCT : CAPN_NULL;
            ret.datasz = (rt_uint32_t)((rt_uint16_t)(val >> 32)) * CAPN_SIZE_UINT64;
            ret.ptrs = (rt_uint32_t)((rt_uint16_t)(val >> 48));
            e = d + ret.datasz + CAPN_SIZE_UINT64 * ret.ptrs;
            break;

        case LIST_PTR:
            ret.type = CAPN_LIST;
            ret.len = val >> 35;

            switch ((val >> 32) & CAPN_PTR_TAG_MASK)
            {
            case VOID_LIST:
                e = d;
                break;
            case BIT_1_LIST:
                ret.type = CAPN_BIT_LIST;
                ret.datasz = (ret.len + CAPN_BIT_MASK_7) / CAPN_BITS_PER_BYTE;
                e = d + ret.datasz;
                break;
            case BYTE_1_LIST:
                ret.datasz = CAPN_SIZE_UINT8;
                e = d + ret.len;
                break;
            case BYTE_2_LIST:
                ret.datasz = CAPN_SIZE_UINT16;
                e = d + ret.len * CAPN_SIZE_UINT16;
                break;
            case BYTE_4_LIST:
                ret.datasz = CAPN_SIZE_UINT32;
                e = d + ret.len * CAPN_SIZE_UINT32;
                break;
            case BYTE_8_LIST:
                ret.datasz = CAPN_SIZE_UINT64;
                e = d + ret.len * CAPN_SIZE_UINT64;
                break;
            case PTR_LIST:
                ret.type = CAPN_PTR_LIST;
                e = d + ret.len * CAPN_SIZE_UINT64;
                break;
            case COMPOSITE_LIST:
                if ((rt_size_t)((d + CAPN_SIZE_UINT64) - s->data) > s->len)
                {
                    read_ok = 0;
                    break;
                }

                val = capn_flip64(*(rt_uint64_t *) d);

                d += CAPN_SIZE_UINT64;
                e = d + ret.len * CAPN_SIZE_UINT64;

                ret.datasz = (rt_uint32_t)((rt_uint16_t)(val >> 32)) * CAPN_SIZE_UINT64;
                ret.ptrs = (rt_uint32_t)((rt_uint16_t)(val >> 48));
                ret.len = (rt_uint32_t)(val) >> 2;
                ret.is_composite_list = 1;

                if ((ret.datasz + CAPN_SIZE_UINT64 * ret.ptrs) * ret.len != (rt_uint32_t)(e - d))
                {
                    read_ok = 0;
                }
                break;
            default:
                read_ok = 0;
                break;
            }
            break;

        default:
            read_ok = 0;
            break;
        }
    }

    if ((0 != read_ok) && (rt_size_t)(e - s->data) <= s->len)
    {
        ret.data = d;
        ret.seg = s;
    }
    else
    {
        capn_ptr zero = {CAPN_NULL, 0, 0, 0, 0, 0, 0, RT_NULL, RT_NULL};
        ret = zero;
    }

    return ret;
}

void capn_resolve(capn_ptr *p)
{
    if (CAPN_FAR_POINTER == p->type)
    {
        *p = read_ptr(p->seg, p->data);
    }
}

/* TODO: should this handle CAPN_BIT_LIST? */
capn_ptr capn_getp(capn_ptr p, rt_int32_t off, rt_int32_t resolve)
{
    capn_ptr ret = {CAPN_FAR_POINTER};
    rt_int32_t valid = 1;
    ret.seg = p.seg;

    capn_resolve(&p);

    switch (p.type)
    {
    case CAPN_LIST:
        /* Return an inner pointer */
        if (off < p.len)
        {
            capn_ptr inner = {CAPN_STRUCT};
            inner.is_list_member = 1;
            inner.data = p.data + off * (p.datasz + CAPN_SIZE_UINT64 * p.ptrs);
            inner.seg = p.seg;
            inner.datasz = p.datasz;
            inner.ptrs = p.ptrs;
            return inner;
        }
        else
        {
            valid = 0;
        }
        break;

    case CAPN_STRUCT:
        if (off >= (rt_int32_t)p.ptrs)
        {
            valid = 0;
        }
        else
        {
            ret.data = p.data + p.datasz + CAPN_SIZE_UINT64 * off;
        }
        break;

    case CAPN_PTR_LIST:
        if (off >= p.len)
        {
            valid = 0;
        }
        else
        {
            ret.data = p.data + CAPN_SIZE_UINT64 * off;
        }
        break;

    default:
        valid = 0;
        break;
    }

    if (0 == valid)
    {
        rt_memset(&p, 0, sizeof(p));
        return p;
    }

    if (0 != resolve)
    {
        ret = read_ptr(ret.seg, ret.data);
    }

    return ret;
}

static void write_ptr_tag(char *d, capn_ptr p, rt_int32_t off)
{
    rt_uint64_t val = (rt_uint64_t)((rt_uint32_t)((rt_int32_t)(off / CAPN_SIZE_UINT64) << 2));

    switch (p.type)
    {
    case CAPN_STRUCT:
        val |= STRUCT_PTR | ((rt_uint64_t)(p.datasz / CAPN_SIZE_UINT64) << 32) | ((rt_uint64_t)(p.ptrs) << 48);
        break;

    case CAPN_LIST:
        if (0 != p.is_composite_list)
        {
            val |= LIST_PTR | ((rt_uint64_t)(COMPOSITE_LIST) << 32)
                   | ((rt_uint64_t)(p.len * (p.datasz / CAPN_SIZE_UINT64 + p.ptrs)) << 35);
        }
        else
        {
            val |= LIST_PTR | ((rt_uint64_t)(p.len) << 35);

            switch (p.datasz)
            {
            case CAPN_SIZE_UINT64:
                val |= ((rt_uint64_t)(BYTE_8_LIST) << 32);
                break;
            case CAPN_SIZE_UINT32:
                val |= ((rt_uint64_t)(BYTE_4_LIST) << 32);
                break;
            case CAPN_SIZE_UINT16:
                val |= ((rt_uint64_t)(BYTE_2_LIST) << 32);
                break;
            case CAPN_SIZE_UINT8:
                val |= ((rt_uint64_t)(BYTE_1_LIST) << 32);
                break;
            case 0:
                val |= ((rt_uint64_t)(VOID_LIST) << 32);
                break;
            default:
                break;
            }
        }
        break;

    case CAPN_BIT_LIST:
        val |= LIST_PTR | ((rt_uint64_t)(BIT_1_LIST) << 32) | ((rt_uint64_t)(p.len) << 35);
        break;

    case CAPN_PTR_LIST:
        val |= LIST_PTR | ((rt_uint64_t)(PTR_LIST) << 32) | ((rt_uint64_t)(p.len) << 35);
        break;

    default:
        val = 0;
        break;
    }

    *(rt_uint64_t *) d = capn_flip64(val);
}

static void write_far_ptr(char *d, struct capn_segment *s, char *tgt)
{
    *(rt_uint64_t *) d = capn_flip64(FAR_PTR | (rt_uint64_t)(tgt - s->data) | ((rt_uint64_t)(s->id) << 32));
}

static void write_double_far(char *d, struct capn_segment *s, char *tgt)
{
    *(rt_uint64_t *) d = capn_flip64(DOUBLE_PTR | (rt_uint64_t)(tgt - s->data) | ((rt_uint64_t)(s->id) << 32));
}

#define NEED_TO_COPY 1

static rt_int32_t write_ptr(struct capn_segment *s, char *d, capn_ptr p)
{
    /* note p.seg can be NULL if its a ptr to static data */
    char *pdata = p.data - CAPN_SIZE_UINT64 * p.is_composite_list;

    if (CAPN_NULL == p.type || (CAPN_STRUCT == p.type && 0 == p.datasz && 0 == p.ptrs))
    {
        write_ptr_tag(d, p, 0);
        return CAPN_ERROR_SUCCESS;
    }
    else if (RT_NULL == p.seg || p.seg->capn != s->capn || 0 != p.is_list_member)
    {
        return NEED_TO_COPY;
    }
    else if (p.seg == s)
    {
        write_ptr_tag(d, p, pdata - d - CAPN_SIZE_UINT64);
        return CAPN_ERROR_SUCCESS;
    }
    else if (0 != p.has_ptr_tag)
    {
        /* By lucky chance, the data has a tag in front
         * of it. This happens when new_object had to move
         * the data to a new segment. */
        write_far_ptr(d, p.seg, pdata - CAPN_SIZE_UINT64);
        return CAPN_ERROR_SUCCESS;
    }
    else if (p.seg->len + CAPN_SIZE_UINT64 <= p.seg->cap)
    {
        /* The target segment has enough room for tag */
        char *t = p.seg->data + p.seg->len;
        write_ptr_tag(t, p, pdata - t - CAPN_SIZE_UINT64);
        write_far_ptr(d, p.seg, t);
        p.seg->len += CAPN_SIZE_UINT64;
        return CAPN_ERROR_SUCCESS;
    }
    else
    {
        /* have to allocate room for a double far
         * pointer */
        char *t;

        if (s->len + (2 * CAPN_SIZE_UINT64) <= s->cap)
        {
            /* Try and allocate in the src segment
             * first. This should improve lookup on
             * read. */
            t = s->data + s->len;
            s->len += (2 * CAPN_SIZE_UINT64);
        }
        else
        {
            t = new_data(s->capn, (2 * CAPN_SIZE_UINT64), &s);
            if (RT_NULL == t)
            {
                return CAPN_ERR_ALLOC_FAILED;
            }
        }

        write_far_ptr(t, p.seg, pdata);
        write_ptr_tag(t + CAPN_SIZE_UINT64, p, 0);
        write_double_far(d, s, t);
        return CAPN_ERROR_SUCCESS;
    }
}

struct copy
{
    struct capn_tree hdr;
    struct capn_ptr to, from;
    char *fbegin, *fend;
};

static capn_ptr new_clone(struct capn_segment *s, capn_ptr p)
{
    switch (p.type)
    {
    case CAPN_STRUCT:
        return capn_new_struct(s, p.datasz, p.ptrs);
    case CAPN_PTR_LIST:
        return capn_new_ptr_list(s, p.len);
    case CAPN_BIT_LIST:
        return capn_new_list1(s, p.len).p;
    case CAPN_LIST:
        return capn_new_list(s, p.len, p.datasz, p.ptrs);
    default:
        return p;
    }
}

static rt_int32_t is_ptr_equal(const struct capn_ptr *a, const struct capn_ptr *b)
{
    return a->data == b->data
        && a->type == b->type
        && a->len == b->len
        && a->datasz == b->datasz
        && a->ptrs == b->ptrs;
}

static rt_int32_t data_size(struct capn_ptr p)
{
    switch (p.type)
    {
    case CAPN_BIT_LIST:
        return p.datasz;
    case CAPN_PTR_LIST:
        return p.len * CAPN_SIZE_UINT64;
    case CAPN_STRUCT:
        return p.datasz + CAPN_SIZE_UINT64 * p.ptrs;
    case CAPN_LIST:
        return p.len * (p.datasz + CAPN_SIZE_UINT64 * p.ptrs) + CAPN_SIZE_UINT64 * p.is_composite_list;
    default:
        return 0;
    }
}

static rt_int32_t copy_ptr(struct capn_segment *seg, char *data,
                           struct capn_ptr *t, struct capn_ptr *f, rt_int32_t *dep)
{
    struct capn *c = seg->capn;
    struct copy *cp = RT_NULL;
    struct capn_tree **xcp;
    char *fbegin = f->data - CAPN_SIZE_UINT64 * f->is_composite_list;
    char *fend = fbegin + data_size(*f);
    rt_int32_t zero_sized = (fend == fbegin);

    /* We always copy list members as it would otherwise be an
     * overlapped pointer (the data is owned by the enclosing list).
     * We do not bother with the overlapped lookup for zero sized
     * structures/lists as they never overlap. Nor do we add them to
     * the copy list as there is no data to be shared by multiple
     * pointers.
     */

    xcp = &c->copy;
    while (RT_NULL != *xcp && 0 == zero_sized)
    {
        cp = (struct copy *) *xcp;
        if (fend <= cp->fbegin)
        {
            xcp = &cp->hdr.link[0];
        }
        else if (cp->fend <= fbegin)
        {
            xcp = &cp->hdr.link[1];
        }
        else if (is_ptr_equal(f, &cp->from))
        {
            /* we already have a copy so just point to that */
            return write_ptr(seg, data, cp->to);
        }
        else
        {
            /* pointer to overlapped data */
            return CAPN_ERR_INVALID_PTR;
        }
    }

    /* no copy found - have to create a new copy */
    *t = new_clone(seg, *f);

    if (write_ptr(seg, data, *t))
    {
        return CAPN_ERR_INVALID_PTR;
    }

    /* add the copy to the copy tree so we can look for overlapping
     * source pointers and handle recursive structures */
    if (0 == zero_sized)
    {
        struct copy *n;
        struct capn_segment *cs = c->copylist;

        /* need to allocate a struct copy */
        if (RT_NULL == cs || cs->len + (rt_int32_t)sizeof(*n) > cs->cap)
        {
            cs = (RT_NULL != c->create_local) ? c->create_local(c->user, sizeof(*n)) : RT_NULL;
            if (RT_NULL == cs)
            {
                /* can't allocate a copy structure */
                return CAPN_ERR_ALLOC_FAILED;
            }
            cs->next = c->copylist;
            c->copylist = cs;
        }

        n = (struct copy *) (cs->data + cs->len);
        cs->len += sizeof(*n);

        n->from = *f;
        n->to = *t;
        n->fbegin = fbegin;
        n->fend = fend;

        *xcp = &n->hdr;
        /* root node (tree was empty) has no parent */
        n->hdr.parent = (xcp != &c->copy) ? &cp->hdr : RT_NULL;

        c->copy = capn_tree_insert(c->copy, &n->hdr);
    }

    /* minimize the number of types the main copy routine has to
     * deal with to just CAPN_LIST and CAPN_PTR_LIST. ptr list only
     * needs t->type, t->len, t->data, t->seg, f->data, f->seg to
     * be valid */
    switch (t->type)
    {
    case CAPN_STRUCT:
        if (0 != t->datasz)
        {
            rt_memcpy(t->data, f->data, t->datasz);
            t->data += t->datasz;
            f->data += t->datasz;
        }
        if (0 != t->ptrs)
        {
            t->type = CAPN_PTR_LIST;
            t->len = t->ptrs;
            (*dep)++;
        }
        return CAPN_ERROR_SUCCESS;

    case CAPN_BIT_LIST:
        rt_memcpy(t->data, f->data, t->datasz);
        return CAPN_ERROR_SUCCESS;

    case CAPN_LIST:
        if (0 == t->len)
        {
            /* empty list - nothing to copy */
        }
        else if (0 != t->ptrs && 0 != t->datasz)
        {
            (*dep)++;
        }
        else if (0 != t->datasz)
        {
            rt_memcpy(t->data, f->data, t->len * t->datasz);
        }
        else if (0 != t->ptrs)
        {
            t->type = CAPN_PTR_LIST;
            t->len *= t->ptrs;
            (*dep)++;
        }
        return CAPN_ERROR_SUCCESS;

    case CAPN_PTR_LIST:
        if (0 != t->len)
        {
            (*dep)++;
        }
        return CAPN_ERROR_SUCCESS;

    default:
        return CAPN_ERR_INVALID_PTR;
    }
}

static void copy_list_member(capn_ptr *t, capn_ptr *f, rt_int32_t *dep)
{
    /* copy struct data */
    rt_int32_t sz = CAPN_MIN(t->datasz, f->datasz);
    rt_memcpy(t->data, f->data, sz);
    rt_memset(t->data + sz, 0, t->datasz - sz);
    t->data += t->datasz;
    f->data += f->datasz;

    /* reset excess pointers */
    sz = CAPN_MIN(t->ptrs, f->ptrs);
    rt_memset(t->data + CAPN_SIZE_UINT64 * sz, 0, CAPN_SIZE_UINT64 * (t->ptrs - sz));

    /* create a pointer list for the main loop to copy */
    if (0 != t->ptrs)
    {
        t->type = CAPN_PTR_LIST;
        t->len = t->ptrs;
        (*dep)++;
    }
}

/* Maximum depth for deep copy traversal stack.
 * Each level uses 2 * sizeof(capn_ptr) = 64 bytes (on 64-bit).
 * Default 8 = 512 bytes on stack, safe for 2KB RT-Thread threads.
 * Override via compiler define if deeper nesting is needed. */
#ifndef CAPN_MAX_COPY_DEPTH
#define CAPN_MAX_COPY_DEPTH 8
#endif

static rt_int32_t do_copy_ptr(struct capn_segment *seg, char *data, capn_ptr tgt,
                              struct capn_ptr *to, struct capn_ptr *from, rt_int32_t *dep)
{
    from[0] = tgt;
    if (copy_ptr(seg, data, to, from, dep))
    {
        return CAPN_ERROR_FAILURE;
    }
    return CAPN_ERROR_SUCCESS;
}

/* TODO: handle CAPN_BIT_LIST and setting from an inner bit list member */
rt_int32_t capn_setp(capn_ptr p, rt_int32_t off, capn_ptr tgt)
{
    struct capn_ptr to[CAPN_MAX_COPY_DEPTH], from[CAPN_MAX_COPY_DEPTH];
    char *data;
    rt_int32_t err, dep = 0;

    capn_resolve(&p);

    if (CAPN_FAR_POINTER == tgt.type && tgt.seg->capn == p.seg->capn)
    {
        rt_uint64_t val = capn_flip64(*(rt_uint64_t *) tgt.data);
        if (FAR_PTR == (val & CAPN_PTR_KIND_MASK))
        {
            *(rt_uint64_t *) p.data = *(rt_uint64_t *) tgt.data;
            return CAPN_ERROR_SUCCESS;
        }
    }

    capn_resolve(&tgt);

    switch (p.type)
    {
    case CAPN_LIST:
        if (off >= p.len || CAPN_STRUCT != tgt.type)
        {
            return CAPN_ERR_OVERFLOW;
        }

        to[0] = p;
        to[0].data += off * (p.datasz + CAPN_SIZE_UINT64 * p.ptrs);
        from[0] = tgt;
        copy_list_member(to, from, &dep);
        break;

    case CAPN_PTR_LIST:
        if (off >= p.len)
        {
            return CAPN_ERR_OVERFLOW;
        }
        data = p.data + CAPN_SIZE_UINT64 * off;
        err = write_ptr(p.seg, data, tgt);
        if (NEED_TO_COPY != err)
        {
            return err;
        }

        /* Depth first copy the source whilst using a pointer stack to
         * maintain the ptr to set and size left to copy at each level.
         * We also maintain a rbtree (capn->copy) of the copies indexed
         * by the source data. This way we can detect overlapped
         * pointers in the source (and bail) and recursive structures
         * (and point to the previous copy).
         */

        if (do_copy_ptr(p.seg, data, tgt, to, from, &dep))
        {
            return CAPN_ERROR_FAILURE;
        }
        break;

    case CAPN_STRUCT:
        if (off >= (rt_int32_t)p.ptrs)
        {
            return CAPN_ERR_OVERFLOW;
        }
        data = p.data + p.datasz + CAPN_SIZE_UINT64 * off;
        err = write_ptr(p.seg, data, tgt);
        if (NEED_TO_COPY != err)
        {
            return err;
        }

        if (do_copy_ptr(p.seg, data, tgt, to, from, &dep))
        {
            return CAPN_ERROR_FAILURE;
        }
        break;

    default:
        return CAPN_ERR_INVALID_TYPE;
    }

    while (0 != dep)
    {
        struct capn_ptr *tc = &to[dep - 1], *tn = &to[dep];
        struct capn_ptr *fc = &from[dep - 1], *fn = &from[dep];

        if (dep + 1 == CAPN_MAX_COPY_DEPTH)
        {
            return CAPN_ERR_DEPTH_EXCEEDED;
        }

        if (0 == tc->len)
        {
            dep--;
            continue;
        }

        if (CAPN_LIST == tc->type)
        {
            *fn = capn_getp(*fc, 0, 1);
            *tn = capn_getp(*tc, 0, 1);

            copy_list_member(tn, fn, &dep);

            fc->data += fc->datasz + CAPN_SIZE_UINT64 * fc->ptrs;
            tc->data += tc->datasz + CAPN_SIZE_UINT64 * tc->ptrs;
            tc->len--;
        }
        else
        {
            /* CAPN_PTR_LIST */
            *fn = read_ptr(fc->seg, fc->data);

            if (0 != fn->type && copy_ptr(tc->seg, tc->data, tn, fn, &dep))
            {
                return CAPN_ERR_INVALID_PTR;
            }

            fc->data += CAPN_SIZE_UINT64;
            tc->data += CAPN_SIZE_UINT64;
            tc->len--;
        }
    }

    return CAPN_ERROR_SUCCESS;
}

/* TODO: handle CAPN_LIST, CAPN_PTR_LIST for bit lists */

rt_int32_t capn_get1(capn_list1 l, rt_int32_t off)
{
    return CAPN_BIT_LIST == l.p.type
        && RT_NULL != l.p.data
        && off >= 0
        && off < l.p.len
        && 0 != (l.p.data[off / CAPN_BITS_PER_BYTE] & (1 << (off % CAPN_BITS_PER_BYTE)));
}

rt_int32_t capn_set1(capn_list1 l, rt_int32_t off, rt_int32_t val)
{
    if (CAPN_BIT_LIST != l.p.type || RT_NULL == l.p.data || off < 0 || off >= l.p.len)
    {
        return CAPN_ERR_OVERFLOW;
    }
    if (0 != val)
    {
        l.p.data[off / CAPN_BITS_PER_BYTE] |= 1 << (off % CAPN_BITS_PER_BYTE);
    }
    else
    {
        l.p.data[off / CAPN_BITS_PER_BYTE] &= ~(1 << (off % CAPN_BITS_PER_BYTE));
    }
    return CAPN_ERROR_SUCCESS;
}

rt_int32_t capn_getv1(capn_list1 l, rt_int32_t off, rt_uint8_t *data, rt_int32_t sz)
{
    /* Note we only support aligned reads */
    rt_int32_t bsz;
    capn_ptr p;
    capn_resolve(&l.p);
    p = l.p;
    if (CAPN_BIT_LIST != p.type || RT_NULL == p.data)
    {
        return CAPN_ERR_INVALID_TYPE;
    }
    if (off < 0 || sz < 0 || 0 != (off & CAPN_BIT_MASK_7))
    {
        return CAPN_ERR_OVERFLOW;
    }

    bsz = (sz + CAPN_BIT_MASK_7) / CAPN_BITS_PER_BYTE;
    off /= CAPN_BITS_PER_BYTE;

    if (off > (rt_int32_t)p.datasz || bsz > (rt_int32_t)p.datasz - off)
    {
        rt_int32_t avail = (rt_int32_t)p.datasz - off;
        if (avail <= 0)
        {
            return 0;
        }
        rt_memcpy(data, p.data + off, (rt_size_t)avail);
        return avail * CAPN_BITS_PER_BYTE;
    }
    else
    {
        rt_memcpy(data, p.data + off, (rt_size_t)bsz);
        return sz;
    }
}

rt_int32_t capn_setv1(capn_list1 l, rt_int32_t off, const rt_uint8_t *data, rt_int32_t sz)
{
    /* Note we only support aligned writes */
    rt_int32_t bsz;
    capn_ptr p;
    capn_resolve(&l.p);
    p = l.p;
    if (CAPN_BIT_LIST != p.type || RT_NULL == p.data)
    {
        return CAPN_ERR_INVALID_TYPE;
    }
    if (off < 0 || sz < 0 || 0 != (off & CAPN_BIT_MASK_7))
    {
        return CAPN_ERR_OVERFLOW;
    }

    bsz = (sz + CAPN_BIT_MASK_7) / CAPN_BITS_PER_BYTE;
    off /= CAPN_BITS_PER_BYTE;

    if (off > (rt_int32_t)p.datasz || bsz > (rt_int32_t)p.datasz - off)
    {
        rt_int32_t avail = (rt_int32_t)p.datasz - off;
        if (avail <= 0)
        {
            return 0;
        }
        rt_memcpy(p.data + off, data, (rt_size_t)avail);
        return avail * CAPN_BITS_PER_BYTE;
    }
    else
    {
        rt_memcpy(p.data + off, data, (rt_size_t)bsz);
        return sz;
    }
}

/* pull out whether we add a tag or not as a define so the unit test can
 * test double far pointers by not creating tags */
#ifndef ADD_TAG
#define ADD_TAG 1
#endif

static void new_object(capn_ptr *p, rt_int32_t bytes)
{
    struct capn_segment *s = p->seg;

    if (RT_NULL == s)
    {
        rt_memset(p, 0, sizeof(*p));
    }
    else if (0 == bytes)
    {
        /* pointer needs to be initialised to get a valid offset on write */
        p->data = s->data + s->len;
    }
    else
    {
        /* all allocations are 8 byte aligned */
        bytes = (bytes + (rt_int32_t)(CAPN_WORD_SIZE - 1)) & ~(rt_int32_t)(CAPN_WORD_SIZE - 1);

        if (s->len + bytes <= s->cap)
        {
            p->data = s->data + s->len;
            s->len += bytes;
        }
        else
        {
            /* add a tag whenever we switch segments so that write_ptr can
             * use it */
            p->data = new_data(s->capn, bytes + ADD_TAG * CAPN_WORD_SIZE, &p->seg);
            if (RT_NULL == p->data)
            {
                rt_memset(p, 0, sizeof(*p));
            }
            else if (0 != ADD_TAG)
            {
                write_ptr_tag(p->data, *p, 0);
                p->data += CAPN_WORD_SIZE;
                p->has_ptr_tag = 1;
            }
        }
    }
}

capn_ptr capn_root(struct capn *c)
{
    capn_ptr r = {CAPN_PTR_LIST};
    r.seg = lookup_segment(c, RT_NULL, 0);
    r.data = (RT_NULL != r.seg) ? r.seg->data : new_data(c, CAPN_SIZE_UINT64, &r.seg);
    r.len = 1;

    if (RT_NULL == r.seg || r.seg->cap < CAPN_SIZE_UINT64)
    {
        rt_memset(&r, 0, sizeof(r));
    }
    else if (r.seg->len < CAPN_SIZE_UINT64)
    {
        r.seg->len = CAPN_SIZE_UINT64;
    }

    return r;
}

capn_ptr capn_new_struct(struct capn_segment *seg, rt_int32_t datasz, rt_int32_t ptrs)
{
    capn_ptr p = {CAPN_STRUCT};
    p.seg = seg;
    p.datasz = (datasz + (rt_int32_t)(CAPN_WORD_SIZE - 1)) & ~(rt_int32_t)(CAPN_WORD_SIZE - 1);
    p.ptrs = ptrs;
    new_object(&p, p.datasz + CAPN_SIZE_UINT64 * p.ptrs);
    return p;
}

capn_ptr capn_new_list(struct capn_segment *seg, rt_int32_t sz, rt_int32_t datasz, rt_int32_t ptrs)
{
    capn_ptr p = {CAPN_LIST};
    p.seg = seg;
    p.len = sz;

    if (0 != ptrs || datasz > CAPN_SIZE_UINT64)
    {
        p.is_composite_list = 1;
        p.datasz = (datasz + (rt_int32_t)(CAPN_WORD_SIZE - 1)) & ~(rt_int32_t)(CAPN_WORD_SIZE - 1);
        p.ptrs = ptrs;
        new_object(&p, p.len * (p.datasz + CAPN_SIZE_UINT64 * p.ptrs) + CAPN_SIZE_UINT64);
        if (RT_NULL != p.data)
        {
            rt_uint64_t hdr = STRUCT_PTR | ((rt_uint64_t)(p.len) << 2) | ((rt_uint64_t)(p.datasz / CAPN_SIZE_UINT64) << 32)
                              | ((rt_uint64_t)(ptrs) << 48);
            *(rt_uint64_t *) p.data = capn_flip64(hdr);
            p.data += CAPN_SIZE_UINT64;
        }
    }
    else if (datasz > CAPN_SIZE_UINT32)
    {
        p.datasz = CAPN_SIZE_UINT64;
        new_object(&p, p.len * CAPN_SIZE_UINT64);
    }
    else if (datasz > CAPN_SIZE_UINT16)
    {
        p.datasz = CAPN_SIZE_UINT32;
        new_object(&p, p.len * CAPN_SIZE_UINT32);
    }
    else
    {
        p.datasz = datasz;
        new_object(&p, p.len * datasz);
    }

    return p;
}

capn_list1 capn_new_list1(struct capn_segment *seg, rt_int32_t sz)
{
    capn_list1 l = {{CAPN_BIT_LIST}};
    l.p.seg = seg;
    l.p.datasz = (sz + CAPN_BIT_MASK_7) / CAPN_BITS_PER_BYTE;
    l.p.len = sz;
    new_object(&l.p, l.p.datasz);
    return l;
}

capn_ptr capn_new_ptr_list(struct capn_segment *seg, rt_int32_t sz)
{
    capn_ptr p = {CAPN_PTR_LIST};
    p.seg = seg;
    p.len = sz;
    p.ptrs = 0;
    p.datasz = 0;
    new_object(&p, sz * CAPN_SIZE_UINT64);
    return p;
}

capn_ptr capn_new_string(struct capn_segment *seg, const char *str, ssize_t sz)
{
    capn_ptr p = {CAPN_LIST};
    p.seg = seg;
    p.len = ((sz >= 0) ? (rt_size_t)sz : rt_strlen(str)) + 1;
    p.datasz = CAPN_SIZE_UINT8;
    new_object(&p, p.len);
    if (RT_NULL != p.data)
    {
        rt_memcpy(p.data, str, p.len - 1);
        p.data[p.len - 1] = '\0';
    }
    return p;
}

capn_text capn_get_text(capn_ptr p, rt_int32_t off, capn_text def)
{
    capn_ptr m = capn_getp(p, off, 1);
    capn_text ret = def;
    if (CAPN_LIST == m.type && CAPN_SIZE_UINT8 == m.datasz && 0 != m.len && 0 == m.data[m.len - 1])
    {
        ret.seg = m.seg;
        ret.str = m.data;
        ret.len = m.len - 1;
    }
    return ret;
}

rt_int32_t capn_set_text(capn_ptr p, rt_int32_t off, capn_text tgt)
{
    capn_ptr m = {CAPN_NULL};
    if (RT_NULL != tgt.seg)
    {
        m.type = CAPN_LIST;
        m.seg = tgt.seg;
        m.data = (char *)tgt.str;
        m.len = tgt.len + 1;
        m.datasz = CAPN_SIZE_UINT8;
    }
    else if (RT_NULL != tgt.str)
    {
        m = capn_new_string(p.seg, tgt.str, tgt.len);
    }
    return capn_setp(p, off, m);
}

capn_data capn_get_data(capn_ptr p, rt_int32_t off)
{
    capn_data ret;
    ret.p = capn_getp(p, off, 1);
    if (CAPN_LIST != ret.p.type || CAPN_SIZE_UINT8 != ret.p.datasz)
    {
        capn_data zero = {{CAPN_NULL, 0, 0, 0, 0, 0, 0, RT_NULL, RT_NULL}};
        ret = zero;
    }
    return ret;
}

#define SZ 8
#include "capn-list.inc"
#undef SZ

#define SZ 16
#include "capn-list.inc"
#undef SZ

#define SZ 32
#include "capn-list.inc"
#undef SZ

#define SZ 64
#include "capn-list.inc"
#undef SZ
