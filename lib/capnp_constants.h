/* capnp_constants.h
 *
 * Copyright (C) 2013 James McKaskill
 * Copyright (C) 2014 Steve Dee
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 *
 * Modified by Raysen Microsystem Technology Co., Ltd. 2025-2026.
 * RT-Thread/MISRA C:2012 adaptation for RS500 platform.
 *
 * Cap'n Proto wire format constants - replaces magic numbers in capnp_c.h
 */

#ifndef CAPNP_CONSTANTS_H
#define CAPNP_CONSTANTS_H

#include "capnp_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Wire format constants                                                      */
/* ========================================================================== */

/* Word size and alignment */
#define CAPN_WORD_SIZE_U                8U     /* 8 bytes per word */
#define CAPN_WORD_ALIGNMENT_U           8U     /* 8-byte alignment for all wire data */

/* Pointer type masks */
#define CAPN_PTR_TAG_MASK_U             7U     /* Mask for lower 3 bits (ptr type) */
#define CAPN_PTR_KIND_MASK_U            3U     /* Mask for lower 2 bits (ptr kind) */

/* Type field bit widths */
#define CAPN_TYPE_FIELD_BITS            4U     /* ptr.type field width */
#define CAPN_HAS_PTR_TAG_BITS           1U     /* ptr.has_ptr_tag field width */
#define CAPN_IS_LIST_MEMBER_BITS        1U     /* ptr.is_list_member field width */
#define CAPN_IS_COMPOSITE_LIST_BITS     1U     /* ptr.is_composite_list field width */
#define CAPN_DATASZ_FIELD_BITS          19U    /* ptr.datasz field width */
#define CAPN_PTRS_FIELD_BITS            16U    /* ptr.ptrs field width */

/* ========================================================================== */
/* Type enumeration values                                                    */
/* ========================================================================== */

#define CAPN_TYPE_NULL                  0U
#define CAPN_TYPE_STRUCT                1U
#define CAPN_TYPE_LIST                  2U
#define CAPN_TYPE_PTR_LIST              3U
#define CAPN_TYPE_BIT_LIST              4U
#define CAPN_TYPE_FAR_POINTER           5U

/* ========================================================================== */
/* Resolve flags                                                              */
/* ========================================================================== */

#ifndef CAPN_RESOLVE_PTR
#define CAPN_RESOLVE_PTR                1U
#endif     /* Resolve through far pointers */
#ifndef CAPN_NO_RESOLVE
#define CAPN_NO_RESOLVE                 0U
#endif     /* Return raw pointer */

/* ========================================================================== */
/* Error codes                                                                */
/* ========================================================================== */

#define CAPN_ERROR_SUCCESS              0U
#define CAPN_ERROR_FAILURE              (-1)   /* Generic error */

/* ========================================================================== */
/* Data type sizes                                                            */
/* ========================================================================== */

#define CAPN_SIZE_UINT8                 1U
#define CAPN_SIZE_UINT16                2U
#define CAPN_SIZE_UINT32                4U
#define CAPN_SIZE_UINT64                8U

/* ========================================================================== */
/* Bit manipulation constants                                                 */
/* ========================================================================== */

#define CAPN_BITS_PER_BYTE              8U
#define CAPN_BIT_MASK_7                 7U     /* Mask for bit offset within byte */
#define CAPN_BIT_SHIFT_3                3U     /* Shift to convert bit offset to byte offset */

/* ========================================================================== */
/* Buffer sizes                                                               */
/* ========================================================================== */

#define CAPN_MIN_WORK_BUFFER_SIZE       4096U  /* Minimum work buffer size for zero-allocation variant */

/* ========================================================================== */
/* Version information                                                        */
/* ========================================================================== */

#define CAPN_VERSION_MAJOR              1U
#define CAPN_VERSION_MINOR              0U
#define CAPN_VERSION_PATCH              0U
#ifndef CAPN_VERSION
#define CAPN_VERSION                    1
#endif

/* ========================================================================== */
/* Endianness detection                                                       */
/* ========================================================================== */

/* Byte order definitions for endianness detection */
#define CAPN_BYTE_ORDER_LITTLE_ENDIAN   1234U
#define CAPN_BYTE_ORDER_BIG_ENDIAN      4321U

#ifdef __cplusplus
}
#endif

#endif /* CAPNP_CONSTANTS_H */