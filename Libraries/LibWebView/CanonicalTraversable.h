/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/HTML/VisibilityState.h>
#include <LibWebView/CanonicalNavigable.h>
#include <LibWebView/Export.h>
#include <LibWebView/SessionHistory.h>

namespace WebView {

class WEBVIEW_API CanonicalTraversable final {
public:
    CanonicalTraversable();
    ~CanonicalTraversable();

    CanonicalNavigable const& root_navigable() const { return m_root_navigable; }
    CanonicalNavigable& root_navigable() { return m_root_navigable; }

    TraversableSessionHistory const& session_history() const { return m_session_history; }
    TraversableSessionHistory& session_history() { return m_session_history; }

    Web::HTML::VisibilityState system_visibility_state() const { return m_system_visibility_state; }
    void set_system_visibility_state(Web::HTML::VisibilityState visibility_state) { m_system_visibility_state = visibility_state; }

private:
    CanonicalNavigable m_root_navigable;
    TraversableSessionHistory m_session_history;
    Web::HTML::VisibilityState m_system_visibility_state { Web::HTML::VisibilityState::Hidden };
};

}
