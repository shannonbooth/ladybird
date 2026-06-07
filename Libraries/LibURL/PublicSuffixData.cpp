/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/String.h>
#include <LibURL/PublicSuffixData.h>

#include <libpsl.h>

namespace URL {

static psl_ctx_t const* public_suffix_context()
{
    static auto const* context = psl_builtin();
    VERIFY(context);
    return context;
}

static constexpr auto public_suffix_match_types = PSL_TYPE_ANY | PSL_TYPE_NO_STAR_RULE;

bool PublicSuffixData::is_public_suffix(StringView host)
{
    if (host.is_empty() || host.ends_with('.'))
        return false;

    ByteString lookup_host { host };
    return psl_is_public_suffix2(public_suffix_context(), lookup_host.characters(), public_suffix_match_types);
}

Optional<String> PublicSuffixData::get_public_suffix(StringView host)
{
    auto remaining_host = host.trim("."sv, TrimMode::Both);
    if (remaining_host.is_empty())
        return OptionalNone {};

    auto const* context = public_suffix_context();
    while (!remaining_host.is_empty()) {
        ByteString lookup_host { remaining_host };
        if (psl_is_public_suffix2(context, lookup_host.characters(), public_suffix_match_types))
            return MUST(String::from_utf8(remaining_host));

        auto next_label_separator = remaining_host.find('.');
        if (!next_label_separator.has_value())
            return OptionalNone {};

        remaining_host = remaining_host.substring_view(*next_label_separator + 1);
    }

    return OptionalNone {};
}

}
