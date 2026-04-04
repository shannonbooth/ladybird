/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

pub mod calendar;
mod character_types;

pub use character_types::{
    canonicalize, code_point_has_general_category, code_point_has_identifier_continue_property,
    code_point_has_identifier_start_property, code_point_has_property, code_point_has_script,
    code_point_has_script_extension, code_point_matches_range_ignoring_case,
};
