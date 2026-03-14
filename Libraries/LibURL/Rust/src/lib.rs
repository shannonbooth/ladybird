/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

pub mod ffi;
pub mod url;

pub use url::{BasicParseOptions, Host, State, Url, basic_parse};
