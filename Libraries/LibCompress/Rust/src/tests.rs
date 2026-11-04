/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use crate::{Compressor, DecompressionFailed, Decompressor, Format};

const FORMATS: [Format; 4] = [Format::Brotli, Format::Zlib, Format::Deflate, Format::Gzip];

fn compress(format: Format, data: &[u8]) -> Vec<u8> {
    let mut compressor = Compressor::new(format);
    let mut compressed = compressor.compress(data).unwrap();
    compressed.extend(compressor.finish().unwrap());
    compressed
}

struct Decompressed {
    output: Vec<u8>,
    unconsumed_input: usize,
}

/// Decompresses the input into pieces of output of at most `piece_size` bytes, until the decoder writes less than a
/// whole piece.
fn decompress_in_pieces(
    decompressor: &mut Decompressor,
    input: &[u8],
    piece_size: usize,
) -> Result<Decompressed, DecompressionFailed> {
    let mut output = Vec::new();
    let mut piece = vec![0; piece_size];
    let mut consumed_input = 0;
    loop {
        let decompressed = decompressor.decompress(&input[consumed_input..], &mut piece)?;
        consumed_input += decompressed.consumed_input;
        output.extend_from_slice(&piece[..decompressed.written_output]);
        if decompressed.written_output < piece_size {
            return Ok(Decompressed {
                output,
                unconsumed_input: input.len() - consumed_input,
            });
        }
    }
}

fn decompress(decompressor: &mut Decompressor, input: &[u8]) -> Result<Decompressed, DecompressionFailed> {
    decompress_in_pieces(decompressor, input, 4096)
}

fn sample_data() -> Vec<u8> {
    (0..100_000u32).map(|i| (i % 251) as u8 ^ (i / 1000) as u8).collect()
}

#[test]
fn round_trip_in_one_piece() {
    let data = sample_data();
    for format in FORMATS {
        let mut decompressor = Decompressor::new(format);
        let decompressed = decompress(&mut decompressor, &compress(format, &data)).unwrap();
        assert_eq!(decompressed.output, data, "{format:?}");
        assert_eq!(decompressed.unconsumed_input, 0, "{format:?}");
        assert!(decompressor.has_reached_end(), "{format:?}");
    }
}

#[test]
fn round_trip_in_pieces() {
    let data = sample_data();
    for format in FORMATS {
        let mut compressor = Compressor::new(format);
        let mut compressed = Vec::new();
        for piece in data.chunks(777) {
            compressed.extend(compressor.compress(piece).unwrap());
        }
        compressed.extend(compressor.finish().unwrap());

        let mut decompressor = Decompressor::new(format);
        let mut output = Vec::new();
        for piece in compressed.chunks(13) {
            let decompressed = decompress(&mut decompressor, piece).unwrap();
            assert_eq!(decompressed.unconsumed_input, 0, "{format:?}");
            output.extend(decompressed.output);
        }
        assert_eq!(output, data, "{format:?}");
        assert!(decompressor.has_reached_end(), "{format:?}");
    }
}

#[test]
fn gives_all_the_output_of_each_piece() {
    let data = vec![b'a'; 1_000_000];
    for format in FORMATS {
        let compressed = compress(format, &data);
        let mut decompressor = Decompressor::new(format);
        let decompressed = decompress(&mut decompressor, &compressed).unwrap();
        assert_eq!(decompressed.output.len(), data.len(), "{format:?}");
    }
}

#[test]
fn gives_output_in_pieces_of_any_size() {
    let data = sample_data();
    for format in FORMATS {
        let compressed = compress(format, &data);
        for piece_size in [1, 7, 100_000] {
            let mut decompressor = Decompressor::new(format);
            let decompressed = decompress_in_pieces(&mut decompressor, &compressed, piece_size).unwrap();
            assert_eq!(decompressed.output, data, "{format:?} {piece_size}");
            assert!(decompressor.has_reached_end(), "{format:?} {piece_size}");
        }
    }
}

#[test]
fn leaves_input_after_the_end_unconsumed() {
    let data = b"Ladybird";
    for format in FORMATS {
        let mut compressed = compress(format, data);
        compressed.extend([0, 1, 2]);

        let mut decompressor = Decompressor::new(format);
        let decompressed = decompress(&mut decompressor, &compressed).unwrap();
        assert_eq!(decompressed.output, data, "{format:?}");
        assert_eq!(decompressed.unconsumed_input, 3, "{format:?}");
        assert!(decompressor.has_reached_end(), "{format:?}");

        let after_end = decompress(&mut decompressor, &[3, 4]).unwrap();
        assert!(after_end.output.is_empty(), "{format:?}");
        assert_eq!(after_end.unconsumed_input, 2, "{format:?}");
    }
}

#[test]
fn does_not_reach_the_end_of_truncated_input() {
    let data = sample_data();
    for format in FORMATS {
        let compressed = compress(format, &data);
        let mut decompressor = Decompressor::new(format);
        let decompressed = decompress(&mut decompressor, &compressed[..compressed.len() - 1]).unwrap();
        assert_eq!(decompressed.unconsumed_input, 0, "{format:?}");
        assert!(!decompressor.has_reached_end(), "{format:?}");
    }
}

#[test]
fn fails_on_corrupt_input() {
    for format in FORMATS {
        let mut compressed = compress(format, &sample_data());
        let middle = compressed.len() / 2;
        compressed[middle] ^= 0xff;
        compressed[middle + 1] ^= 0xff;

        let mut decompressor = Decompressor::new(format);
        assert!(decompress(&mut decompressor, &compressed).is_err(), "{format:?}");
    }
}

#[test]
fn a_gzip_stream_has_one_member() {
    let mut compressed = compress(Format::Gzip, b"one");
    let second_member = compress(Format::Gzip, b"two");
    compressed.extend(&second_member);

    let mut decompressor = Decompressor::new(Format::Gzip);
    let decompressed = decompress(&mut decompressor, &compressed).unwrap();
    assert_eq!(decompressed.output, b"one");
    assert_eq!(decompressed.unconsumed_input, second_member.len());
}

#[test]
fn rejects_brotli_large_window() {
    // "Hello World", compressed with the large window extension and a window of 25 bits.
    let compressed = [17, 25, 20, 0, 2, 72, 101, 108, 108, 111, 32, 87, 111, 114, 108, 100, 3];
    let mut decompressor = Decompressor::new(Format::Brotli);
    assert!(decompress(&mut decompressor, &compressed).is_err());
}
