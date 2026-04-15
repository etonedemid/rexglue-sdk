/**
 * @file        hook.h
 * @brief       Shim macros and type aliases for kernel export hooks
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <rex/ppc/function.h>
#include <rex/ppc/types.h>

//=============================================================================
// Export hook macros (aliases for XBOXKRNL_EXPORT variants)
//=============================================================================

#define REX_EXPORT(name, function) XBOXKRNL_EXPORT(name, function)
#define REX_EXPORT_STUB(name)      XBOXKRNL_EXPORT_STUB(name)

//=============================================================================
// Mapped pointer type aliases (guest-address-aware host pointer types)
//=============================================================================

using mapped_void    = rex::PPCPointer<void>;
using mapped_u8      = rex::PPCPointer<uint8_t>;
using mapped_u16     = rex::PPCPointer<rex::be_u16>;
using mapped_u32     = rex::PPCPointer<rex::be_u32>;
using mapped_u64     = rex::PPCPointer<rex::be_u64>;
using mapped_i8      = rex::PPCPointer<int8_t>;
using mapped_i16     = rex::PPCPointer<rex::be_i16>;
using mapped_i32     = rex::PPCPointer<rex::be_i32>;
using mapped_i64     = rex::PPCPointer<rex::be_i64>;
using mapped_f32     = rex::PPCPointer<rex::be_f32>;
using mapped_f64     = rex::PPCPointer<rex::be_f64>;
using mapped_string  = rex::PPCPointer<char>;
using mapped_wstring = rex::PPCPointer<char16_t>;
