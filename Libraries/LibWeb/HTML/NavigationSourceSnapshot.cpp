/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibIPC/Decoder.h>
#include <LibIPC/Encoder.h>
#include <LibWeb/HTML/NavigationSourceSnapshot.h>

namespace IPC {

template<>
ErrorOr<void> encode(Encoder& encoder, Web::HTML::NavigationSourceSnapshot const& snapshot)
{
    TRY(encoder.encode(snapshot.has_transient_activation));
    TRY(encoder.encode(snapshot.sandboxing_flags));
    TRY(encoder.encode(snapshot.allows_downloading));
    TRY(encoder.encode(snapshot.source_policy_container));
    return {};
}

template<>
ErrorOr<Web::HTML::NavigationSourceSnapshot> decode(Decoder& decoder)
{
    return Web::HTML::NavigationSourceSnapshot {
        .has_transient_activation = TRY(decoder.decode<bool>()),
        .sandboxing_flags = TRY(decoder.decode<Web::HTML::SandboxingFlagSet>()),
        .allows_downloading = TRY(decoder.decode<bool>()),
        .source_policy_container = TRY(decoder.decode<Web::HTML::SerializedPolicyContainer>()),
    };
}

}
