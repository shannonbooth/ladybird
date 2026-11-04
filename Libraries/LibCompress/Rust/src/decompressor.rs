/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use core::fmt;

use brotli::{BrotliDecompressStream, BrotliResult, BrotliState, HeapAlloc, HuffmanCode};
use flate2::{Decompress, FlushDecompress, Status};

use crate::Format;

type BrotliDecoder = BrotliState<HeapAlloc<u8>, HeapAlloc<u32>, HeapAlloc<HuffmanCode>>;

/// The decoder of a format.
enum Decoder {
    Brotli(Box<BrotliDecoder>),
    // zlib, raw deflate and gzip, which zlib-rs decodes.
    Flate(Decompress),
}

/// The window that gzip data is decoded with, the largest that the format allows.
const GZIP_WINDOW_BITS: u8 = 15;

/// What decompressing some input into some room for output did.
pub struct Decompressed {
    pub consumed_input: usize,
    pub written_output: usize,
}

/// The input is not data of the format, with the decoder's description of why.
#[derive(Debug)]
pub struct DecompressionFailed(String);

impl fmt::Display for DecompressionFailed {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// Decompresses data that arrives in pieces, giving all that it can decompress of each one.
pub struct Decompressor {
    decoder: Decoder,
    has_reached_end: bool,
}

impl Decompressor {
    pub fn new(format: Format) -> Self {
        let decoder = match format {
            // NB: new_strict() decodes RFC 7932 brotli only, without its large window extension.
            Format::Brotli => Decoder::Brotli(Box::new(BrotliDecoder::new_strict(
                HeapAlloc::default(),
                HeapAlloc::default(),
                HeapAlloc::default(),
            ))),
            Format::Zlib => Decoder::Flate(Decompress::new(true)),
            Format::Deflate => Decoder::Flate(Decompress::new(false)),
            Format::Gzip => Decoder::Flate(Decompress::new_gzip(GZIP_WINDOW_BITS)),
        };
        Self {
            decoder,
            has_reached_end: false,
        }
    }

    /// Whether the decoder has reached the end of the compressed data.
    pub fn has_reached_end(&self) -> bool {
        self.has_reached_end
    }

    /// Decompresses the next bytes of the data into the output, until the decoder runs out of input or of room for
    /// output. It writes less than the room it is given only once it has given all that it can of the input and of what
    /// it kept of the bytes before them. Bytes after the end of the compressed data are left unconsumed.
    pub fn decompress(&mut self, input: &[u8], output: &mut [u8]) -> Result<Decompressed, DecompressionFailed> {
        if self.has_reached_end {
            return Ok(Decompressed {
                consumed_input: 0,
                written_output: 0,
            });
        }
        match &mut self.decoder {
            Decoder::Brotli(decoder) => decompress_brotli(decoder, input, output, &mut self.has_reached_end),
            Decoder::Flate(decoder) => decompress_flate(decoder, input, output, &mut self.has_reached_end),
        }
    }
}

fn decompress_brotli(
    decoder: &mut BrotliDecoder,
    input: &[u8],
    output: &mut [u8],
    has_reached_end: &mut bool,
) -> Result<Decompressed, DecompressionFailed> {
    let mut available_in = input.len();
    let mut input_offset = 0;
    let mut available_out = output.len();
    let mut output_offset = 0;
    let mut total_out = 0;
    let result = BrotliDecompressStream(
        &mut available_in,
        &mut input_offset,
        input,
        &mut available_out,
        &mut output_offset,
        output,
        &mut total_out,
        decoder,
    );
    match result {
        BrotliResult::NeedsMoreOutput | BrotliResult::NeedsMoreInput => {}
        BrotliResult::ResultSuccess => *has_reached_end = true,
        BrotliResult::ResultFailure => return Err(DecompressionFailed(format!("{:?}", decoder.error_code))),
    }
    Ok(Decompressed {
        consumed_input: input_offset,
        written_output: output_offset,
    })
}

fn decompress_flate(
    decoder: &mut Decompress,
    input: &[u8],
    output: &mut [u8],
    has_reached_end: &mut bool,
) -> Result<Decompressed, DecompressionFailed> {
    let mut consumed_input = 0;
    let mut written_output = 0;
    loop {
        let (total_in, total_out) = (decoder.total_in(), decoder.total_out());
        let status = decoder
            .decompress(
                &input[consumed_input..],
                &mut output[written_output..],
                FlushDecompress::None,
            )
            .map_err(|error| DecompressionFailed(error.to_string()))?;
        consumed_input +=
            usize::try_from(decoder.total_in() - total_in).expect("a slice has fewer bytes than usize::MAX");
        written_output +=
            usize::try_from(decoder.total_out() - total_out).expect("a slice has fewer bytes than usize::MAX");
        match status {
            Status::StreamEnd => {
                *has_reached_end = true;
                break;
            }
            // NB: The decoder stops when it runs out of input or of room for its output, and fails to make progress
            //     with BufError, so it has given all it can of this input once it stops short of both.
            Status::Ok if consumed_input < input.len() && written_output < output.len() => {}
            Status::Ok | Status::BufError => break,
        }
    }
    Ok(Decompressed {
        consumed_input,
        written_output,
    })
}
