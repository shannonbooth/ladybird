/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

mod ffi;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EncodeItem {
    Byte(u8),
    Error(u32),
}

unsafe extern "C" fn on_string_bytes(ctx: *mut c_void, data: *const u8, len: usize) {
    let out = unsafe { &mut *(ctx as *mut String) };
    let bytes = unsafe { std::slice::from_raw_parts(data, len) };
    *out = String::from_utf8_lossy(bytes).into_owned();
}

struct EncodeCallbacks<'a> {
    on_item: &'a mut dyn FnMut(EncodeItem),
}

unsafe extern "C" fn on_encode_byte_with_callbacks(ctx: *mut c_void, byte: u8) {
    let callbacks = unsafe { &mut *(ctx as *mut EncodeCallbacks<'_>) };
    (callbacks.on_item)(EncodeItem::Byte(byte));
}

unsafe extern "C" fn on_encode_error_with_callbacks(ctx: *mut c_void, error: u32) {
    let callbacks = unsafe { &mut *(ctx as *mut EncodeCallbacks<'_>) };
    (callbacks.on_item)(EncodeItem::Error(error));
}

pub fn get_output_encoding(encoding: &str) -> String {
    let mut result = String::new();
    unsafe {
        ffi::textcodec_rust_get_output_encoding(
            encoding.as_ptr(),
            encoding.len(),
            std::ptr::addr_of_mut!(result) as *mut c_void,
            on_string_bytes,
        );
    }
    result
}

pub fn encode(encoding: &str, input: &str) -> Option<Vec<EncodeItem>> {
    let mut result = Vec::new();
    let did_succeed = encode_into(encoding, input, |item| result.push(item));
    did_succeed.then_some(result)
}

pub fn encode_into(encoding: &str, input: &str, mut on_item: impl FnMut(EncodeItem)) -> bool {
    let mut callbacks = EncodeCallbacks {
        on_item: &mut on_item,
    };

    unsafe {
        ffi::textcodec_rust_encode(
            encoding.as_ptr(),
            encoding.len(),
            input.as_ptr(),
            input.len(),
            std::ptr::addr_of_mut!(callbacks) as *mut c_void,
            on_encode_byte_with_callbacks,
            on_encode_error_with_callbacks,
        )
    }
}
