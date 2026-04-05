/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Utf8View.h>
#include <LibUnicode/CharacterTypes.h>
#include <LibUnicode/IDNA.h>
#include <LibUnicode/RustFFI.h>

namespace Unicode::FFI {

extern "C" uint32_t unicode_rust_canonicalize(uint32_t code_point, bool unicode_mode)
{
    return Unicode::canonicalize(code_point, unicode_mode);
}

extern "C" bool unicode_rust_code_point_has_general_category(uint32_t code_point, uint32_t general_category, bool case_sensitive)
{
    return Unicode::code_point_has_general_category(code_point, static_cast<Unicode::GeneralCategory>(general_category), case_sensitive ? CaseSensitivity::CaseSensitive : CaseSensitivity::CaseInsensitive);
}

extern "C" bool unicode_rust_code_point_has_property(uint32_t code_point, uint32_t property, bool case_sensitive)
{
    return Unicode::code_point_has_property(code_point, static_cast<Unicode::Property>(property), case_sensitive ? CaseSensitivity::CaseSensitive : CaseSensitivity::CaseInsensitive);
}

extern "C" bool unicode_rust_code_point_has_identifier_start_property(uint32_t code_point)
{
    return Unicode::code_point_has_identifier_start_property(code_point);
}

extern "C" bool unicode_rust_code_point_has_identifier_continue_property(uint32_t code_point)
{
    return Unicode::code_point_has_identifier_continue_property(code_point);
}

extern "C" bool unicode_rust_code_point_has_script(uint32_t code_point, uint32_t script)
{
    return Unicode::code_point_has_script(code_point, static_cast<Unicode::Script>(script));
}

extern "C" bool unicode_rust_code_point_has_script_extension(uint32_t code_point, uint32_t script)
{
    return Unicode::code_point_has_script_extension(code_point, static_cast<Unicode::Script>(script));
}

extern "C" bool unicode_rust_code_point_matches_range_ignoring_case(uint32_t code_point, uint32_t from, uint32_t to, bool unicode_mode)
{
    return Unicode::code_point_matches_range_ignoring_case(code_point, from, to, unicode_mode);
}

extern "C" void unicode_rust_idna_to_ascii(uint8_t const* domain, size_t domain_length, ToAsciiOptions const* options, void* ctx, FfiIdnaResultFn on_success)
{
    Unicode::IDNA::ToAsciiOptions const cpp_options {
        options->check_hyphens ? Unicode::IDNA::CheckHyphens::Yes : Unicode::IDNA::CheckHyphens::No,
        options->check_bidi ? Unicode::IDNA::CheckBidi::Yes : Unicode::IDNA::CheckBidi::No,
        options->check_joiners ? Unicode::IDNA::CheckJoiners::Yes : Unicode::IDNA::CheckJoiners::No,
        options->use_std3_ascii_rules ? Unicode::IDNA::UseStd3AsciiRules::Yes : Unicode::IDNA::UseStd3AsciiRules::No,
        options->transitional_processing ? Unicode::IDNA::TransitionalProcessing::Yes : Unicode::IDNA::TransitionalProcessing::No,
        options->verify_dns_length ? Unicode::IDNA::VerifyDnsLength::Yes : Unicode::IDNA::VerifyDnsLength::No,
        options->ignore_invalid_punycode ? Unicode::IDNA::IgnoreInvalidPunycode::Yes : Unicode::IDNA::IgnoreInvalidPunycode::No,
    };

    auto result = Unicode::IDNA::to_ascii(Utf8View { StringView { domain, domain_length } }, cpp_options);
    if (result.is_error())
        return;

    auto const ascii_bytes = result.value().bytes_as_string_view();
    on_success(ctx, reinterpret_cast<uint8_t const*>(ascii_bytes.characters_without_null_termination()), ascii_bytes.length());
}

}
