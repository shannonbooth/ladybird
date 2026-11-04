/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::borrow::Cow;

use encoding_rs::{EncoderResult, UTF_16BE, UTF_16LE};

/// An encoding, https://encoding.spec.whatwg.org/#encoding
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Encoding(pub(crate) &'static encoding_rs::Encoding);

/// What an encoder gives for the input: a byte of output, or a code point that the encoding cannot represent.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EncodeItem {
    Byte(u8),
    Error(u32),
}

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

    // https://encoding.spec.whatwg.org/#get-an-output-encoding
    pub fn output_encoding(self) -> Self {
        // 1. If encoding is replacement or UTF-16BE/LE, then return UTF-8.
        if self == Self::REPLACEMENT || self.0 == UTF_16BE || self.0 == UTF_16LE {
            return Self::UTF_8;
        }

        // 2. Return encoding.
        self
    }

    pub fn encode_into(self, input: &str, mut on_item: impl FnMut(EncodeItem)) -> bool {
        let mut encoder = self.0.new_encoder();
        let mut total_read = 0usize;
        let Some(output_capacity) = encoder.max_buffer_length_from_utf8_without_replacement(input.len()) else {
            return false;
        };
        let mut output = Vec::with_capacity(output_capacity);

        loop {
            let (result, read) =
                encoder.encode_from_utf8_to_vec_without_replacement(&input[total_read..], &mut output, true);
            total_read += read;

            for byte in output.drain(..) {
                on_item(EncodeItem::Byte(byte));
            }

            match result {
                EncoderResult::InputEmpty => return true,
                EncoderResult::OutputFull => return false,
                EncoderResult::Unmappable(unmappable) => on_item(EncodeItem::Error(unmappable as u32)),
            }
        }
    }

    pub fn encode_utf16_into(self, input: &[u16], mut on_item: impl FnMut(EncodeItem)) -> bool {
        let mut encoder = self.0.new_encoder();
        let mut total_read = 0;
        let mut output = [0; 4096];
        loop {
            let (result, read, written) =
                encoder.encode_from_utf16_without_replacement(&input[total_read..], &mut output, true);
            total_read += read;
            for byte in &output[..written] {
                on_item(EncodeItem::Byte(*byte));
            }
            match result {
                EncoderResult::InputEmpty => return true,
                EncoderResult::OutputFull => {}
                EncoderResult::Unmappable(character) => on_item(EncodeItem::Error(character as u32)),
            }
        }
    }
}

/// https://encoding.spec.whatwg.org/#utf-8-decode-without-bom
pub fn utf8_decode_without_bom(bytes: &[u8]) -> Cow<'_, str> {
    encoding_rs::UTF_8.decode_without_bom_handling(bytes).0
}
