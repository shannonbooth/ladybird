/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//! DecompressionStream, https://compression.spec.whatwg.org/#decompressionstream

use core::cell::RefCell;

use ak::Utf16String;
use libcompress_rust::Decompressor;
use libjs_rust::api::{EmbedderClass, EmbedderValue, MarkedVec, Trace, Value};

use crate::bindings::conversions::{BufferSource, create_uint8_array};
use crate::bindings::interface::Ref;
use crate::bindings::interface::Wrappable;
use crate::bindings::{CallContext, Exception, WebIdlResult};
use crate::compression::buffer_source_of_chunk;
use crate::compression::compression_stream::CompressionFormat;
use crate::streams::generic_transform_stream::{TransformSteps, create_and_set_up};
use crate::streams::readable_stream::ReadableStream;
use crate::streams::transform_stream::TransformStream;
use crate::streams::writable_stream::WritableStream;

#[derive(Trace)]
#[gc(crate = libjs_rust)]
pub(crate) struct DecompressionStream {
    // https://streams.spec.whatwg.org/#generictransformstream-transform
    transform: Ref<TransformStream>,
}

impl Wrappable for DecompressionStream {}

impl DecompressionStream {
    // https://compression.spec.whatwg.org/#dom-decompressionstream-decompressionstream
    pub(crate) fn construct(cx: &mut CallContext, format: CompressionFormat) -> WebIdlResult<Self> {
        // 1. If format is unsupported in DecompressionStream, then throw a TypeError.
        // 2. Set this's format to format.
        let context = DecompressionContext::new(format);

        // 3. Let transformAlgorithm be an algorithm which takes a chunk argument and runs the decompress and enqueue a chunk
        //    algorithm with this and chunk.
        // 4. Let flushAlgorithm be an algorithm which takes no argument and runs the decompress flush and enqueue algorithm with this.
        // 5. Set this's transform to a new TransformStream.
        // 6. Set up this's transform with transformAlgorithm set to transformAlgorithm and flushAlgorithm set to flushAlgorithm.
        Ok(Self {
            transform: create_and_set_up(cx, context),
        })
    }

    // https://streams.spec.whatwg.org/#dom-generictransformstream-readable
    pub(crate) fn readable(&self, _: &mut CallContext) -> WebIdlResult<Ref<ReadableStream>> {
        // The readable getter steps are to return this's transform.[[readable]].
        Ok(self.transform.readable_stream())
    }

    // https://streams.spec.whatwg.org/#dom-generictransformstream-writable
    pub(crate) fn writable(&self, _: &mut CallContext) -> WebIdlResult<Ref<WritableStream>> {
        // The writable getter steps are to return this's transform.[[writable]].
        Ok(self.transform.writable_stream())
    }
}

/// https://compression.spec.whatwg.org/#compression-context, which the algorithms of the stream's transform run on.
#[derive(Trace)]
#[gc(crate = libjs_rust)]
struct DecompressionContext {
    #[gc(untraced)]
    decompressor: RefCell<Decompressor>,
}

static DECOMPRESSION_CONTEXT_CLASS: EmbedderClass<DecompressionContext> = EmbedderClass::new("DecompressionContext");

impl EmbedderValue for DecompressionContext {
    const CLASS: &'static EmbedderClass<Self> = &DECOMPRESSION_CONTEXT_CLASS;
}

/// The largest piece that the output of decompressing is split into.
const OUTPUT_PIECE_SIZE: usize = 64 * 1024;

/// The pieces that the output of decompressing some input splits into, and how many bytes at the end of the input the
/// decompressor did not consume.
struct Decompressed<'vm> {
    arrays: MarkedVec<'vm, Value>,
    unconsumed_input: usize,
}

impl DecompressionContext {
    fn new(format: CompressionFormat) -> Self {
        Self {
            decompressor: RefCell::new(Decompressor::new(format.into())),
        }
    }

    /// Decompresses the bytes of the chunk into Uint8Arrays. They are only enqueued afterwards, since enqueueing can
    /// run script, which could change the bytes of the chunk.
    fn decompress<'vm>(&self, cx: &'vm CallContext, chunk: &BufferSource) -> WebIdlResult<Decompressed<'vm>> {
        let mut decompressor = self.decompressor.borrow_mut();
        let mut piece = vec![0; OUTPUT_PIECE_SIZE];
        let arrays = MarkedVec::new(cx.vm());
        let mut consumed_input = 0;
        loop {
            // SAFETY: Decompressing neither runs script nor allocates a cell.
            let (decompressed, input_length) = unsafe {
                chunk.with_bytes(|input| {
                    let decompressed = decompressor.decompress(&input[consumed_input..], &mut piece);
                    (decompressed, input.len())
                })
            };
            let decompressed = decompressed.map_err(|error| decompression_failed("chunk", &error.to_string()))?;
            consumed_input += decompressed.consumed_input;
            if decompressed.written_output > 0 {
                let array = create_uint8_array(cx, &piece[..decompressed.written_output])?;
                arrays.push(Value::from_object(array));
            }
            if decompressed.written_output < piece.len() {
                return Ok(Decompressed {
                    arrays,
                    unconsumed_input: input_length - consumed_input,
                });
            }
        }
    }

    fn has_reached_end_of_compressed_input(&self) -> bool {
        self.decompressor.borrow().has_reached_end()
    }
}

fn decompression_failed(step: &str, error: &str) -> Exception {
    Exception::TypeError(Utf16String::from_utf8(&format!("Unable to decompress {step}: {error}")))
}

impl TransformSteps for DecompressionContext {
    // https://compression.spec.whatwg.org/#decompress-and-enqueue-a-chunk
    fn transform(&self, cx: &CallContext, transform: Ref<TransformStream>, chunk: Value) -> WebIdlResult<()> {
        // 1. If chunk is not a BufferSource type, then throw a TypeError.
        let chunk = buffer_source_of_chunk(cx, chunk)?;

        // 2. Let buffer be the result of decompressing chunk with ds's format and context. If this results in an error,
        //    then throw a TypeError.
        // 3. If buffer is empty, return.
        // AD-HOC: Skip steps 4 and 5 instead, so that step 6 also rejects a chunk that gives no output.
        //         See: https://github.com/whatwg/compression/issues/78
        // 4. Let arrays be the result of splitting buffer into one or more non-empty pieces and converting them
        //    into Uint8Arrays.
        let decompressed = self.decompress(cx, &chunk)?;

        // 5. For each Uint8Array array of arrays, enqueue array in ds's transform.
        for &array in decompressed.arrays.values().iter() {
            TransformStream::enqueue(transform, cx.vm(), array)?;
        }

        // 6. If the end of the compressed input has been reached, and ds's context has not fully consumed chunk, then
        //    throw a TypeError.
        if self.has_reached_end_of_compressed_input() && decompressed.unconsumed_input > 0 {
            return Err(decompression_failed(
                "chunk",
                "Data after the end of the compressed input",
            ));
        }
        Ok(())
    }

    // https://compression.spec.whatwg.org/#decompress-flush-and-enqueue
    fn flush(&self, _: &CallContext, _: Ref<TransformStream>) -> WebIdlResult<()> {
        // 1. Let buffer be the result of decompressing an empty input with ds's format and context, with the finish
        //    flag.
        // 2. If buffer is not empty:
        //     1. Let arrays be the result of splitting buffer into one or more non-empty pieces and converting them
        //        into Uint8Arrays.
        //     2. For each Uint8Array array of arrays, enqueue array in ds's transform.
        // NB: Each chunk gave all the output that the decoder could make of it, so an empty input gives no more.

        // 3. If the end of the compressed input has not been reached, then throw a TypeError.
        if !self.has_reached_end_of_compressed_input() {
            return Err(decompression_failed("flush", "The compressed input ended early"));
        }
        Ok(())
    }
}
