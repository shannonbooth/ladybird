/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/CanonicalTraversable.h>

namespace WebView {

CanonicalTraversable::CanonicalTraversable()
{
    auto& root_state = m_root_navigable.replicated_state();
    root_state.is_traversable = true;
    root_state.is_top_level_traversable = true;
}

CanonicalTraversable::~CanonicalTraversable() = default;

}
