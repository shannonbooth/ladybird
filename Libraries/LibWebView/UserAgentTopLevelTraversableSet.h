/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Badge.h>
#include <AK/Function.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Vector.h>
#include <LibWebCommon/HTML/CrossProcessId.h>
#include <LibWebView/Export.h>
#include <LibWebView/Forward.h>
#include <LibWebView/TabId.h>

namespace WebView {

// https://html.spec.whatwg.org/multipage/document-sequences.html#top-level-traversable-set
class WEBVIEW_API UserAgentTopLevelTraversableSet {
public:
    static UserAgentTopLevelTraversableSet& the();
    ~UserAgentTopLevelTraversableSet();

    CanonicalTraversable& add(Badge<CanonicalTraversable>, NonnullOwnPtr<CanonicalTraversable>);
    void remove(Badge<CanonicalTraversable>, CanonicalTraversable&);
    void for_each(Function<IterationDecision(CanonicalTraversable&)> const&) const;
    CanonicalTraversable* find(TabId) const;
    CanonicalTraversable* find_by_handle(StringView) const;
    CanonicalTraversable* traversable_containing(Web::HTML::CrossProcessId) const;
    CanonicalNavigable* navigable_with_active_browsing_context(CanonicalBrowsingContext const&) const;

private:
    UserAgentTopLevelTraversableSet() = default;
    Vector<NonnullOwnPtr<CanonicalTraversable>> m_traversables;
};

}
