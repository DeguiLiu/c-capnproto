/* str.c
 *
 * Copyright (C) 2013 James McKaskill
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 *
 * Modified by Raysen Microsystem Technology Co., Ltd. 2025-2026.
 * RT-Thread/MISRA C:2012 adaptation for RS500 platform.
 */

#include "str.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef va_copy
#ifdef _MSC_VER
#define va_copy(d, s) d = s
#elif defined __GNUC__
#define va_copy(d, s) __builtin_va_copy(d, s)
#else
#error
#endif
#endif

char str_static[] = "\0";

void str_reserve(struct str *v, int32_t sz)
{
    if (sz < v->cap)
    {
        return;
    }

    v->cap = (v->cap * 2) + 16;
    if (sz > v->cap)
    {
        v->cap = (sz + 8) & ~7;
    }

    if (str_static == v->str)
    {
        v->str = NULL;
    }

    v->str = realloc(v->str, (size_t)(v->cap + 1));
}

void str_add(struct str *v, const char *str, int32_t sz)
{
    if (sz < 0)
    {
        sz = (int32_t)strlen(str);
    }
    str_reserve(v, v->len + sz);
    memcpy(v->str + v->len, str, (size_t)sz);
    v->len += sz;
    v->str[v->len] = '\0';
}

int32_t str_vaddf(struct str *v, const char *format, va_list ap)
{
    str_reserve(v, v->len + 1);

    for (;;)
    {
        int32_t ret;

        char *buf = v->str + v->len;
        int32_t bufsz = v->cap - v->len;

        va_list aq;
        va_copy(aq, ap);

        /* We initialise buf[bufsz] to \0 to detect when snprintf runs out of
         * buffer by seeing whether it overwrites it.
         */
        buf[bufsz] = '\0';
        ret = vsnprintf(buf, (size_t)(bufsz + 1), format, aq);

        if (ret > bufsz)
        {
            /* snprintf has told us the size of buffer required (ISO C99
             * behavior)
             */
            str_reserve(v, v->len + ret);
        }
        else if (ret >= 0)
        {
            /* success */
            v->len += ret;
            return ret;
        }
        else if ('\0' != buf[bufsz])
        {
            /* snprintf has returned an error but has written to the end of the
             * buffer (MSVC behavior). The buffer is not large enough so grow
             * and retry. This can also occur with a format error if it occurs
             * right on the boundary, but then we grow the buffer and can
             * figure out its an error next time around.
             */
            str_reserve(v, v->len + bufsz + 1);
        }
        else
        {
            /* snprintf has returned an error but has not written to the last
             * character in the buffer. We have a format error.
             */
            return -1;
        }
    }
}

int32_t str_addf(struct str *v, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    return str_vaddf(v, format, ap);
}

char *strf(struct str *v, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    str_reset(v);
    str_vaddf(v, format, ap);
    return v->str;
}
