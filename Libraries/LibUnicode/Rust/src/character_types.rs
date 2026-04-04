/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

unsafe extern "C" {
    fn unicode_rust_canonicalize(code_point: u32, unicode_mode: bool) -> u32;
    fn unicode_rust_code_point_has_general_category(
        code_point: u32,
        general_category: u32,
        case_sensitive: bool,
    ) -> bool;
    fn unicode_rust_code_point_has_property(
        code_point: u32,
        property: u32,
        case_sensitive: bool,
    ) -> bool;
    fn unicode_rust_code_point_has_identifier_start_property(code_point: u32) -> bool;
    fn unicode_rust_code_point_has_identifier_continue_property(code_point: u32) -> bool;
    fn unicode_rust_code_point_has_script(code_point: u32, script: u32) -> bool;
    fn unicode_rust_code_point_has_script_extension(code_point: u32, script: u32) -> bool;
    fn unicode_rust_code_point_matches_range_ignoring_case(
        code_point: u32,
        from: u32,
        to: u32,
        unicode_mode: bool,
    ) -> bool;
}

#[unsafe(no_mangle)]
pub extern "C" fn canonicalize(code_point: u32, unicode_mode: bool) -> u32 {
    // SAFETY: Pure mapping over a Unicode code point.
    unsafe { unicode_rust_canonicalize(code_point, unicode_mode) }
}

#[unsafe(no_mangle)]
pub extern "C" fn code_point_has_general_category(
    code_point: u32,
    general_category: u32,
    case_sensitive: bool,
) -> bool {
    // SAFETY: Pure predicate over a Unicode code point and category identifier.
    unsafe {
        unicode_rust_code_point_has_general_category(code_point, general_category, case_sensitive)
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn code_point_has_property(
    code_point: u32,
    property: u32,
    case_sensitive: bool,
) -> bool {
    // SAFETY: Pure predicate over a Unicode code point and property identifier.
    unsafe { unicode_rust_code_point_has_property(code_point, property, case_sensitive) }
}

#[unsafe(no_mangle)]
pub extern "C" fn code_point_has_identifier_start_property(code_point: u32) -> bool {
    // SAFETY: Pure predicate over a Unicode code point.
    unsafe { unicode_rust_code_point_has_identifier_start_property(code_point) }
}

#[unsafe(no_mangle)]
pub extern "C" fn code_point_has_identifier_continue_property(code_point: u32) -> bool {
    // SAFETY: Pure predicate over a Unicode code point.
    unsafe { unicode_rust_code_point_has_identifier_continue_property(code_point) }
}

#[unsafe(no_mangle)]
pub extern "C" fn code_point_has_script(code_point: u32, script: u32) -> bool {
    // SAFETY: Pure predicate over a Unicode code point and script identifier.
    unsafe { unicode_rust_code_point_has_script(code_point, script) }
}

#[unsafe(no_mangle)]
pub extern "C" fn code_point_has_script_extension(code_point: u32, script: u32) -> bool {
    // SAFETY: Pure predicate over a Unicode code point and script identifier.
    unsafe { unicode_rust_code_point_has_script_extension(code_point, script) }
}

#[unsafe(no_mangle)]
pub extern "C" fn code_point_matches_range_ignoring_case(
    code_point: u32,
    from: u32,
    to: u32,
    unicode_mode: bool,
) -> bool {
    // SAFETY: Pure predicate over Unicode code points and bounds.
    unsafe {
        unicode_rust_code_point_matches_range_ignoring_case(code_point, from, to, unicode_mode)
    }
}
