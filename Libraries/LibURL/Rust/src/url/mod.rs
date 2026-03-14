/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

mod host;
mod parser;
mod percent_encoding;
mod scheme;
mod serialize;
mod types;

pub(crate) use self::parser::basic_parse_into;
pub(crate) use self::scheme::{default_port_for_scheme, is_special_scheme, special_schemes};
pub(crate) use self::types::ExcludeFragment;
pub use self::types::{Host, State, Url};

#[derive(Debug, Default)]
pub struct BasicParseOptions<'a> {
    pub base_url: Option<&'a Url>,
    pub url: Option<&'a mut Url>,
    state_override: Option<State>,
    encoding: Option<&'a str>,
}

impl<'a> BasicParseOptions<'a> {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn base_url(mut self, base_url: &'a Url) -> Self {
        self.base_url = Some(base_url);
        self
    }

    pub fn url(mut self, url: &'a mut Url) -> Self {
        self.url = Some(url);
        self
    }

    pub fn state_override(mut self, state: impl Into<Option<State>>) -> Self {
        self.state_override = state.into();
        self
    }

    pub fn encoding(mut self, encoding: impl Into<Option<&'a str>>) -> Self {
        self.encoding = encoding.into();
        self
    }
}

pub fn basic_parse(input: &str, options: BasicParseOptions<'_>) -> Option<Url> {
    if let Some(existing_url) = options.url {
        let mut wrapper_options = BasicParseOptions::new()
            .state_override(options.state_override)
            .encoding(options.encoding);
        wrapper_options.base_url = options.base_url;
        if basic_parse_into(input, existing_url, &wrapper_options, true) {
            return Some(existing_url.clone());
        }
        return None;
    }

    let mut url = Url::default();
    if basic_parse_into(input, &mut url, &options, false) {
        Some(url)
    } else {
        None
    }
}
