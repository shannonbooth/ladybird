/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibIPC/Decoder.h>
#include <LibIPC/Encoder.h>
#include <LibWebCommon/HTML/DirectiveState.h>

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::HTML::DirectiveState const& directive_state)
{
    TRY(encoder.encode(directive_state.id));
    TRY(encoder.encode(directive_state.value));
    return {};
}

template<>
ErrorOr<Web::HTML::DirectiveState> IPC::decode(Decoder& decoder)
{
    auto id = TRY(decoder.decode<Web::HTML::CrossProcessId>());
    auto value = TRY(decoder.decode<Optional<String>>());
    return Web::HTML::DirectiveState { .id = id, .value = move(value) };
}
