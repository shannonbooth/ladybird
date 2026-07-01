/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/String.h>
#include <LibIPC/Forward.h>
#include <LibURL/Origin.h>
#include <LibURL/URL.h>
#include <LibWeb/Export.h>

namespace Web::HTML {

// Cross-process-visible state for a navigable, authored by the UI process and
// mirrored into WebContent processes that hold a remote proxy for it.
struct ReplicatedNavigableState {
    String target_name;
    Optional<URL::Origin> active_document_origin;
    Optional<URL::URL> active_document_top_level_creation_url;
    Optional<URL::Origin> active_document_top_level_origin;
    bool active_document_is_fully_active { false };
    bool is_closed { false };
    size_t active_document_child_navigable_count { 0 };
    bool is_traversable { false };
    bool is_top_level_traversable { false };
};

}

namespace IPC {

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::HTML::ReplicatedNavigableState const&);

template<>
WEB_API ErrorOr<Web::HTML::ReplicatedNavigableState> decode(Decoder&);

}
