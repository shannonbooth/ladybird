/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::io::Write;

use brotli::CompressorWriter;
use flate2::Compression;
use flate2::write::{DeflateEncoder, GzEncoder, ZlibEncoder};

use crate::Format;

/// The size of the buffer that the brotli encoder compresses into before it writes to its output.
const BROTLI_BUFFER_SIZE: usize = 4096;
/// The quality and the window size that brotli uses by default.
const BROTLI_QUALITY: u32 = 11;
const BROTLI_WINDOW_BITS: u32 = 22;

/// The encoder of a format, which writes what it compresses to a buffer that it keeps, until it finishes.
enum Encoder {
    Brotli(Box<CompressorWriter<Vec<u8>>>),
    Zlib(ZlibEncoder<Vec<u8>>),
    Deflate(DeflateEncoder<Vec<u8>>),
    Gzip(GzEncoder<Vec<u8>>),
}

/// Compresses data that arrives in pieces, giving what it has compressed after each one.
pub struct Compressor {
    encoder: Encoder,
}

const WRITING_TO_A_VEC_SUCCEEDS: &str = "an encoder that writes to a Vec does not fail";

impl Compressor {
    pub fn new(format: Format) -> Self {
        let encoder = match format {
            Format::Brotli => Encoder::Brotli(Box::new(CompressorWriter::new(
                Vec::new(),
                BROTLI_BUFFER_SIZE,
                BROTLI_QUALITY,
                BROTLI_WINDOW_BITS,
            ))),
            Format::Zlib => Encoder::Zlib(ZlibEncoder::new(Vec::new(), Compression::default())),
            Format::Deflate => Encoder::Deflate(DeflateEncoder::new(Vec::new(), Compression::default())),
            Format::Gzip => Encoder::Gzip(GzEncoder::new(Vec::new(), Compression::default())),
        };
        Self { encoder }
    }

    /// Compresses the next bytes of the data, returning what the encoder output, which can be nothing.
    pub fn compress(&mut self, bytes: &[u8]) -> Vec<u8> {
        let output = match &mut self.encoder {
            Encoder::Brotli(encoder) => {
                encoder.write_all(bytes).expect(WRITING_TO_A_VEC_SUCCEEDS);
                encoder.get_mut()
            }
            Encoder::Zlib(encoder) => {
                encoder.write_all(bytes).expect(WRITING_TO_A_VEC_SUCCEEDS);
                encoder.get_mut()
            }
            Encoder::Deflate(encoder) => {
                encoder.write_all(bytes).expect(WRITING_TO_A_VEC_SUCCEEDS);
                encoder.get_mut()
            }
            Encoder::Gzip(encoder) => {
                encoder.write_all(bytes).expect(WRITING_TO_A_VEC_SUCCEEDS);
                encoder.get_mut()
            }
        };
        core::mem::take(output)
    }

    /// Finishes the compressed data, returning the rest of what the encoder output.
    pub fn finish(self) -> Vec<u8> {
        match self.encoder {
            // NB: The brotli encoder finishes the compressed data when it gives up its output.
            Encoder::Brotli(encoder) => encoder.into_inner(),
            Encoder::Zlib(encoder) => encoder.finish().expect(WRITING_TO_A_VEC_SUCCEEDS),
            Encoder::Deflate(encoder) => encoder.finish().expect(WRITING_TO_A_VEC_SUCCEEDS),
            Encoder::Gzip(encoder) => encoder.finish().expect(WRITING_TO_A_VEC_SUCCEEDS),
        }
    }
}
