/* capnp_compat.h
 *
 * RT-Thread / Standard C compatibility layer.
 * When building under RT-Thread, uses rtdef.h types directly.
 * Otherwise, provides equivalent typedefs from <stdint.h>.
 *
 * Modified by Raysen Microsystem Technology Co., Ltd. 2025-2026.
 */

#ifndef CAPNP_COMPAT_H
#define CAPNP_COMPAT_H

#ifdef RT_USING_CAPNP
    /* RT-Thread environment: use native types */
    #include <rtdef.h>
#else
    /* Standard C environment (Linux/Windows host) */
    #include <stdint.h>
    #include <stddef.h>

    typedef int8_t      rt_int8_t;
    typedef uint8_t     rt_uint8_t;
    typedef int16_t     rt_int16_t;
    typedef uint16_t    rt_uint16_t;
    typedef int32_t     rt_int32_t;
    typedef uint32_t    rt_uint32_t;
    typedef int64_t     rt_int64_t;
    typedef uint64_t    rt_uint64_t;
    typedef size_t      rt_size_t;
    typedef int32_t     rt_base_t;
    typedef uint32_t    rt_ubase_t;

    #ifndef RT_NULL
        #define RT_NULL     NULL
    #endif
    #ifndef RT_TRUE
        #define RT_TRUE     1
    #endif
    #ifndef RT_FALSE
        #define RT_FALSE    0
    #endif

    /* Standard C function mappings for RT-Thread API */
    #include <string.h>
    #include <stdio.h>
    #include <stdlib.h>
    #define rt_memset   memset
    #define rt_memcpy   memcpy
    #define rt_memmove  memmove
    #define rt_strlen   strlen
    #define rt_strncpy  strncpy
    #define rt_malloc   malloc
    #define rt_calloc   calloc
    #define rt_realloc  realloc
    #define rt_free     free
#endif /* RT_USING_CAPNP */

#endif /* CAPNP_COMPAT_H */
