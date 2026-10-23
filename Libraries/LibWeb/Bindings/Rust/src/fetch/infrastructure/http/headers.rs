/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//! Headers, https://fetch.spec.whatwg.org/#terminology-headers

use ak::Span;

// NB: The checks stay in C++ with HTTP::Header (Libraries/LibHTTP/Header.cpp); Libraries/LibWeb/Fetch/Headers.cpp
//     defines these functions over them.
unsafe extern "C" {
    fn web_fetch_is_header_name(name: Span<'_, u8>) -> bool;
    fn web_fetch_is_header_value(value: Span<'_, u8>) -> bool;
    fn web_fetch_is_forbidden_request_header(name: Span<'_, u8>, value: Span<'_, u8>) -> bool;
    fn web_fetch_is_forbidden_response_header_name(name: Span<'_, u8>) -> bool;
    fn web_fetch_is_no_cors_safelisted_request_header_name(name: Span<'_, u8>) -> bool;
    fn web_fetch_is_privileged_no_cors_request_header_name(name: Span<'_, u8>) -> bool;
    fn web_fetch_is_no_cors_safelisted_request_header(name: Span<'_, u8>, value: Span<'_, u8>) -> bool;
}

pub(crate) fn is_header_name(name: &[u8]) -> bool {
    // SAFETY: The name outlives the call.
    unsafe { web_fetch_is_header_name(Span::new(name)) }
}

pub(crate) fn is_header_value(value: &[u8]) -> bool {
    // SAFETY: The value outlives the call.
    unsafe { web_fetch_is_header_value(Span::new(value)) }
}

pub(crate) fn is_forbidden_request_header(name: &[u8], value: &[u8]) -> bool {
    // SAFETY: The name and the value outlive the call.
    unsafe { web_fetch_is_forbidden_request_header(Span::new(name), Span::new(value)) }
}

pub(crate) fn is_forbidden_response_header_name(name: &[u8]) -> bool {
    // SAFETY: The name outlives the call.
    unsafe { web_fetch_is_forbidden_response_header_name(Span::new(name)) }
}

pub(crate) fn is_no_cors_safelisted_request_header_name(name: &[u8]) -> bool {
    // SAFETY: The name outlives the call.
    unsafe { web_fetch_is_no_cors_safelisted_request_header_name(Span::new(name)) }
}

pub(crate) fn is_privileged_no_cors_request_header_name(name: &[u8]) -> bool {
    // SAFETY: The name outlives the call.
    unsafe { web_fetch_is_privileged_no_cors_request_header_name(Span::new(name)) }
}

pub(crate) fn is_no_cors_safelisted_request_header(name: &[u8], value: &[u8]) -> bool {
    // SAFETY: The name and the value outlive the call.
    unsafe { web_fetch_is_no_cors_safelisted_request_header(Span::new(name), Span::new(value)) }
}

// https://fetch.spec.whatwg.org/#http-whitespace-byte
fn is_http_whitespace_byte(byte: u8) -> bool {
    // An HTTP whitespace byte is 0x0A (LF), 0x0D (CR), or an HTTP tab or space byte.
    matches!(byte, 0x0A | 0x0D | 0x09 | 0x20)
}

// https://fetch.spec.whatwg.org/#concept-header-value-normalize
pub(crate) fn normalize(potential_value: &[u8]) -> &[u8] {
    // To normalize a byte sequence potentialValue, remove any leading and trailing HTTP whitespace bytes from
    // potentialValue.
    let start = potential_value
        .iter()
        .position(|&byte| !is_http_whitespace_byte(byte))
        .unwrap_or(potential_value.len());
    let end = potential_value
        .iter()
        .rposition(|&byte| !is_http_whitespace_byte(byte))
        .map_or(start, |index| index + 1);
    &potential_value[start..end]
}
