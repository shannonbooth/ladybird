/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Utf8View.h>
#include <LibTextCodec/Decoder.h>
#include <LibTextCodec/Encoder.h>
#include <LibTextCodec/Export.h>
#include <LibTextCodec/RustFFI.h>

namespace TextCodec::FFI {

extern "C" TEXTCODEC_API void textcodec_rust_get_output_encoding(uint8_t const* encoding, size_t encoding_length, void* ctx, FfiBytesFn on_result)
{
    if (!encoding)
        return;
    auto output_encoding = TextCodec::get_output_encoding(StringView { encoding, encoding_length });
    on_result(ctx, reinterpret_cast<uint8_t const*>(output_encoding.characters_without_null_termination()), output_encoding.length());
}

extern "C" TEXTCODEC_API bool textcodec_rust_encode(uint8_t const* encoding, size_t encoding_length, uint8_t const* input, size_t input_length, void* ctx, FfiByteFn on_byte, FfiCodePointFn on_error)
{
    if (!encoding || !input)
        return false;

    auto encoder = TextCodec::encoder_for(StringView { encoding, encoding_length });
    if (!encoder.has_value())
        return false;

    auto result = encoder->process(
        Utf8View { StringView { input, input_length } },
        [&](u8 byte) -> ErrorOr<void> {
            on_byte(ctx, byte);
            return {};
        },
        [&](u32 error) -> ErrorOr<void> {
            on_error(ctx, error);
            return {};
        });
    return !result.is_error();
}

}
