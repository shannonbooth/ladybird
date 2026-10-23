/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//! The C ABI through which LibWeb's C++ creates the Headers objects of requests and responses, and changes their
//! guards and header lists as fetching does, which Libraries/LibWeb/Fetch/Headers.h declares. A cell is the cell of a
//! Headers object.

use core::ffi::c_void;

use ak::Span;
use libjs_rust::api::Vm;

use super::headers::{Guard, HeaderList, Headers, HeadersInit, TypeErrorMessage};
use crate::bindings::conversions::ByteString;
use crate::bindings::interface::{Ref, allocate, cell_from_abi, cell_into_abi};

/// The Headers object whose cell C++ lends, for as long as C++ keeps the cell alive.
///
/// # Safety
///
/// `cell` must be the live cell of a Headers object, and it must outlive `'a`.
unsafe fn headers_from_abi<'a>(cell: *mut c_void) -> &'a Headers {
    // SAFETY: The caller passes a live cell of a Headers object.
    let object: Ref<Headers> = unsafe { cell_from_abi(cell) };
    // SAFETY: The caller keeps the cell alive for 'a.
    unsafe { &*object.as_ptr() }
}

fn guard_from_abi(guard: u8) -> Guard {
    match guard {
        0 => Guard::Immutable,
        1 => Guard::Request,
        2 => Guard::RequestNoCors,
        3 => Guard::Response,
        4 => Guard::None,
        _ => panic!("C++ passes a headers guard"),
    }
}

/// Lends the message of the TypeError to C++ through `error`, and returns whether there was none.
///
/// # Safety
///
/// `error` must be valid to write to.
unsafe fn result_into_abi(result: Result<(), TypeErrorMessage>, error: *mut Span<'static, u8>) -> bool {
    match result {
        Ok(()) => true,
        Err(message) => {
            // SAFETY: The caller passes a valid pointer.
            unsafe { error.write(Span::new(message.as_bytes())) };
            false
        }
    }
}

/// A new Headers object whose header list is the HTTP::HeaderList, one reference to which it adopts, and whose guard
/// is "none".
///
/// # Safety
///
/// `vm` must be the live VM, and `header_list` a live HTTP::HeaderList.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn web_headers_create(vm: *mut c_void, header_list: *mut c_void) -> *mut c_void {
    assert!(!vm.is_null(), "C++ passes the VM");
    // SAFETY: The caller passes the live VM.
    let vm = unsafe { &*vm.cast::<Vm>() };
    // SAFETY: The caller passes a live header list, one reference to which it gives up.
    let header_list = unsafe { HeaderList::adopt(header_list) };
    cell_into_abi(allocate(vm, |_| Headers::new(header_list)))
}

/// The HTTP::HeaderList of the Headers object, which the object keeps alive.
///
/// # Safety
///
/// `cell` must be the live cell of a Headers object.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn web_headers_header_list(cell: *mut c_void) -> *mut c_void {
    // SAFETY: The caller passes a live cell.
    unsafe { headers_from_abi(cell) }.header_list().as_ptr()
}

/// # Safety
///
/// `cell` must be the live cell of a Headers object.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn web_headers_guard(cell: *mut c_void) -> u8 {
    // SAFETY: The caller passes a live cell.
    unsafe { headers_from_abi(cell) }.guard() as u8
}

/// # Safety
///
/// `cell` must be the live cell of a Headers object.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn web_headers_set_guard(cell: *mut c_void, guard: u8) {
    // SAFETY: The caller passes a live cell.
    unsafe { headers_from_abi(cell) }.set_guard(guard_from_abi(guard));
}

/// Appends the header to the Headers object, https://fetch.spec.whatwg.org/#concept-headers-append. Returns false,
/// with the message of the TypeError in `error`, if that throws.
///
/// # Safety
///
/// `cell` must be the live cell of a Headers object, and `error` valid to write to.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn web_headers_append(
    cell: *mut c_void,
    name: Span<'_, u8>,
    value: Span<'_, u8>,
    error: *mut Span<'static, u8>,
) -> bool {
    // SAFETY: The caller passes a live cell.
    let headers = unsafe { headers_from_abi(cell) };
    // SAFETY: The caller passes a valid pointer.
    unsafe { result_into_abi(headers.append_header(name.as_slice(), value.as_slice()), error) }
}

/// Fills the Headers object with the HeadersInit, https://fetch.spec.whatwg.org/#concept-headers-fill: a record whose
/// entries are each a key and a value if `is_record`, and otherwise a sequence of sequences. Returns false, with the
/// message of the TypeError in `error`, if that throws.
///
/// # Safety
///
/// `cell` must be the live cell of a Headers object, and `error` valid to write to.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn web_headers_fill(
    cell: *mut c_void,
    is_record: bool,
    entries: Span<'_, Span<'_, Span<'_, u8>>>,
    error: *mut Span<'static, u8>,
) -> bool {
    // SAFETY: The caller passes a live cell.
    let headers = unsafe { headers_from_abi(cell) };
    let entries = entries.as_slice().iter().map(|entry| {
        entry
            .as_slice()
            .iter()
            .map(|string| ByteString::from_bytes(string.as_slice().to_vec()))
    });
    let object = if is_record {
        HeadersInit::Record(
            entries
                .map(|mut entry| {
                    let key = entry.next().expect("an entry of a record has a key");
                    let value = entry.next().expect("an entry of a record has a value");
                    (key, value)
                })
                .collect(),
        )
    } else {
        HeadersInit::Sequence(entries.map(Iterator::collect).collect())
    };
    // SAFETY: The caller passes a valid pointer.
    unsafe { result_into_abi(headers.fill(object), error) }
}
