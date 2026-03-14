/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

pub type FfiBytesFn = unsafe extern "C" fn(*mut c_void, *const u8, usize);
pub type FfiByteFn = unsafe extern "C" fn(*mut c_void, u8);
pub type FfiCodePointFn = unsafe extern "C" fn(*mut c_void, u32);

unsafe extern "C" {
    pub(crate) fn textcodec_rust_get_output_encoding(
        encoding: *const u8,
        encoding_length: usize,
        ctx: *mut c_void,
        on_result: FfiBytesFn,
    );
    pub(crate) fn textcodec_rust_encode(
        encoding: *const u8,
        encoding_length: usize,
        input: *const u8,
        input_length: usize,
        ctx: *mut c_void,
        on_byte: FfiByteFn,
        on_error: FfiCodePointFn,
    ) -> bool;
}
