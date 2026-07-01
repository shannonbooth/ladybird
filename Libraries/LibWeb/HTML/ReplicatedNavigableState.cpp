/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibIPC/Decoder.h>
#include <LibIPC/Encoder.h>
#include <LibWeb/HTML/ReplicatedNavigableState.h>

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::HTML::ReplicatedNavigableState const& state)
{
    TRY(encoder.encode(state.target_name));
    TRY(encoder.encode(state.active_document_origin));
    TRY(encoder.encode(state.active_document_top_level_creation_url));
    TRY(encoder.encode(state.active_document_top_level_origin));
    TRY(encoder.encode(state.active_document_is_fully_active));
    TRY(encoder.encode(state.is_closed));
    TRY(encoder.encode(state.active_document_child_navigable_count));
    TRY(encoder.encode(state.is_traversable));
    TRY(encoder.encode(state.is_top_level_traversable));
    return {};
}

template<>
ErrorOr<Web::HTML::ReplicatedNavigableState> IPC::decode(Decoder& decoder)
{
    auto target_name = TRY(decoder.decode<String>());
    auto active_document_origin = TRY(decoder.decode<Optional<URL::Origin>>());
    auto active_document_top_level_creation_url = TRY(decoder.decode<Optional<URL::URL>>());
    auto active_document_top_level_origin = TRY(decoder.decode<Optional<URL::Origin>>());
    auto active_document_is_fully_active = TRY(decoder.decode<bool>());
    auto is_closed = TRY(decoder.decode<bool>());
    auto active_document_child_navigable_count = TRY(decoder.decode<size_t>());
    auto is_traversable = TRY(decoder.decode<bool>());
    auto is_top_level_traversable = TRY(decoder.decode<bool>());

    return Web::HTML::ReplicatedNavigableState {
        .target_name = move(target_name),
        .active_document_origin = move(active_document_origin),
        .active_document_top_level_creation_url = move(active_document_top_level_creation_url),
        .active_document_top_level_origin = move(active_document_top_level_origin),
        .active_document_is_fully_active = active_document_is_fully_active,
        .is_closed = is_closed,
        .active_document_child_navigable_count = active_document_child_navigable_count,
        .is_traversable = is_traversable,
        .is_top_level_traversable = is_top_level_traversable,
    };
}
