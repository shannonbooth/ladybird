/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#![allow(clippy::module_inception)]

mod canonicalization;
mod component;
mod constructor_string_parser;
mod init;
mod options;
mod part;
mod pattern;
mod pattern_error;
mod pattern_parser;
mod string;
mod tokenizer;

pub use canonicalization::{
    canonicalize_a_hash, canonicalize_a_hostname, canonicalize_a_password, canonicalize_a_pathname,
    canonicalize_a_port, canonicalize_a_protocol, canonicalize_a_search, canonicalize_a_username,
    canonicalize_an_ipv6_hostname, canonicalize_an_opaque_pathname,
};
pub use component::{
    Component, GroupMatch, RegularExpression, Result as ComponentResult,
    protocol_component_matches_a_special_scheme,
};
pub use constructor_string_parser::ConstructorStringParser;
pub use init::{Init, PatternProcessType, process_a_url_pattern_init};
pub use options::Options;
pub use part::Part;
pub use pattern::{IgnoreCase, Input, MatchInput, Pattern, Result};
pub use pattern_error::{ErrorInfo, PatternErrorOr};
pub use pattern_parser::{EncodingCallback, PatternParser};
pub use string::{
    escape_a_pattern_string, escape_a_regexp_string, full_wildcard_regexp_value,
    generate_a_pattern_string, generate_a_segment_wildcard_regexp,
};
pub use tokenizer::{Token, Tokenizer};
