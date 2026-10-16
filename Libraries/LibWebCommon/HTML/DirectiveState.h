/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/String.h>
#include <LibIPC/Forward.h>
#include <LibWebCommon/Export.h>
#include <LibWebCommon/HTML/CrossProcessId.h>

namespace Web::HTML {

// https://wicg.github.io/scroll-to-text-fragment/#directive-state
struct DirectiveState {
    // NB: Entries that share a directive state name it by this ID, in every process that holds them.
    CrossProcessId id;

    // value, the fragment directive ASCII string or null, initially null.
    Optional<String> value;
};

}

namespace IPC {

template<>
WEBCOMMON_API ErrorOr<void> encode(Encoder&, Web::HTML::DirectiveState const&);

template<>
WEBCOMMON_API ErrorOr<Web::HTML::DirectiveState> decode(Decoder&);

}
