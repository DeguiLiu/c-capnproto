/* capn-list.inc
 *
 * Copyright (C) 2013 James McKaskill
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 *
 * Modified by Raysen Microsystem Technology Co., Ltd. 2025-2026.
 * RT-Thread/MISRA C:2012 adaptation for RS500 platform.
 */

#define CAT2(A, B) A##B
#define CAT(A, B) CAT2(A, B)
#define UINT_T CAT(CAT(rt_uint, SZ), _t)
#define LIST_T CAT(capn_list, SZ)
#define FLIP CAT(capn_flip, SZ)

UINT_T CAT(capn_get, SZ)(LIST_T l, rt_int32_t off)
{
    char *d;
    capn_ptr p;
    capn_resolve(&l.p);
    p = l.p;
    if (off >= p.len)
    {
        return 0;
    }

    switch (p.type)
    {
    case CAPN_LIST:
        if (p.datasz < SZ / 8)
        {
            return 0;
        }
        d = p.data + off * (p.datasz + 8 * p.ptrs);
        return FLIP(*(UINT_T *)d);

    case CAPN_PTR_LIST:
        d = struct_ptr(p.seg, p.data + 8 * off, SZ / 8);
        if (RT_NULL != d)
        {
            return FLIP(*(UINT_T *)d);
        }
        else
        {
            return 0;
        }

    default:
        return 0;
    }
}

rt_int32_t CAT(capn_getv, SZ)(LIST_T l, rt_int32_t off, UINT_T *to, rt_int32_t sz)
{
    rt_int32_t i;
    capn_ptr p;
    capn_resolve(&l.p);
    p = l.p;
    if (off < 0 || sz < 0 || off > p.len)
    {
        return CAPN_ERR_OVERFLOW;
    }
    if (sz > p.len - off)
    {
        sz = p.len - off;
    }

    switch (p.type)
    {
    case CAPN_LIST:
        if ((rt_int32_t)p.datasz == SZ / 8 && 0 == p.ptrs && (8 == SZ || CAPN_LITTLE))
        {
            rt_memcpy(to, p.data + off, (rt_size_t)(sz * (SZ / 8)));
            return sz;
        }
        else if (p.datasz < SZ / 8)
        {
            return CAPN_ERR_INVALID_TYPE;
        }

        for (i = 0; i < sz; i++)
        {
            char *d = p.data + (i + off) * (p.datasz + 8 * p.ptrs);
            to[i] = FLIP(*(UINT_T *)d);
        }
        return sz;

    case CAPN_PTR_LIST:
        for (i = 0; i < sz; i++)
        {
            char *d = struct_ptr(p.seg, p.data + 8 * (i + off), SZ / 8);
            if (RT_NULL != d)
            {
                to[i] = FLIP(*(UINT_T *)d);
            }
            else
            {
                return CAPN_ERR_INVALID_PTR;
            }
        }
        return sz;

    default:
        return CAPN_ERR_INVALID_TYPE;
    }
}

rt_int32_t CAT(capn_set, SZ)(LIST_T l, rt_int32_t off, UINT_T v)
{
    char *d;
    capn_ptr p;
    capn_resolve(&l.p);
    p = l.p;
    if (off >= p.len)
    {
        return CAPN_ERR_OVERFLOW;
    }

    switch (p.type)
    {
    case CAPN_LIST:
        if (p.datasz < SZ / 8)
        {
            return CAPN_ERR_INVALID_TYPE;
        }
        d = p.data + off * (p.datasz + 8 * p.ptrs);
        *(UINT_T *)d = FLIP(v);
        return 0;

    case CAPN_PTR_LIST:
        d = struct_ptr(p.seg, p.data + 8 * off, SZ / 8);
        if (RT_NULL == d)
        {
            return CAPN_ERR_INVALID_PTR;
        }
        *(UINT_T *)d = FLIP(v);
        return 0;

    default:
        return CAPN_ERR_INVALID_TYPE;
    }
}

rt_int32_t CAT(capn_setv, SZ)(LIST_T l, rt_int32_t off, const UINT_T *from, rt_int32_t sz)
{
    rt_int32_t i;
    capn_ptr p = l.p;
    if (off < 0 || sz < 0 || off > p.len)
    {
        return CAPN_ERR_OVERFLOW;
    }
    if (sz > p.len - off)
    {
        sz = p.len - off;
    }

    switch (p.type)
    {
    case CAPN_LIST:
        if ((rt_int32_t)p.datasz == SZ / 8 && 0 == p.ptrs && (8 == SZ || CAPN_LITTLE))
        {
            rt_memcpy(p.data + off, from, (rt_size_t)(sz * (SZ / 8)));
            return sz;
        }
        else if (p.datasz < SZ / 8)
        {
            return CAPN_ERR_INVALID_TYPE;
        }

        for (i = 0; i < sz; i++)
        {
            char *d = p.data + (i + off) * (p.datasz + 8 * p.ptrs);
            *(UINT_T *)d = FLIP(from[i]);
        }
        return sz;

    case CAPN_PTR_LIST:
        for (i = 0; i < sz; i++)
        {
            char *d = struct_ptr(p.seg, p.data + 8 * (i + off), SZ / 8);
            if (RT_NULL != d)
            {
                *(UINT_T *)d = FLIP(from[i]);
            }
            else
            {
                return CAPN_ERR_INVALID_PTR;
            }
        }
        return sz;

    default:
        return CAPN_ERR_INVALID_TYPE;
    }
}

LIST_T CAT(capn_new_list, SZ)(struct capn_segment *seg, rt_int32_t sz)
{
    LIST_T l = {{CAPN_LIST}};
    l.p.seg = seg;
    l.p.len = sz;
    l.p.datasz = SZ / 8;
    new_object(&l.p, sz * (SZ / 8));
    return l;
}

#undef CAT2
#undef CAT
#undef UINT_T
#undef LIST_T
#undef FLIP
