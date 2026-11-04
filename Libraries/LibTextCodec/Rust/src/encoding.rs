/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::borrow::Cow;

/// An encoding, https://encoding.spec.whatwg.org/#encoding
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Encoding(pub(crate) &'static encoding_rs::Encoding);

impl Encoding {
    pub const UTF_8: Self = Self(encoding_rs::UTF_8);
    pub const REPLACEMENT: Self = Self(encoding_rs::REPLACEMENT);

    /// Getting an encoding from a label, https://encoding.spec.whatwg.org/#concept-encoding-get
    pub fn for_label(label: &[u8]) -> Option<Self> {
        encoding_rs::Encoding::for_label(label).map(Self)
    }

    /// https://encoding.spec.whatwg.org/#name
    pub fn name(self) -> &'static str {
        self.0.name()
    }

    /// https://encoding.spec.whatwg.org/#decode, with this encoding as the fallback encoding.
    pub fn decode(self, bytes: &[u8]) -> Cow<'_, str> {
        self.0.decode(bytes).0
    }
}
