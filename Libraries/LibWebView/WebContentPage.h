/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/RefPtr.h>
#include <LibWeb/HTML/CrossProcessId.h>
#include <LibWeb/Page/PageId.h>
#include <LibWebView/Export.h>
#include <LibWebView/Forward.h>

namespace WebView {

// A page a WebContent process holds for the UI process. It holds the graph of one tab, and displays that tab when it
// is the view's page.
struct WEBVIEW_API WebContentPage {
    RefPtr<WebContentClient> client;
    Web::PageId id { 0 };

    bool is_open() const;
    CanonicalTraversable* traversable() const;
    // The view when this page displays its tab.
    Optional<ViewImplementation&> view() const;
    // The view of the tab this page holds, whichever page displays it.
    Optional<ViewImplementation&> owning_view() const;
    Optional<CanonicalNavigable&> hosted_navigable(Web::HTML::CrossProcessId) const;
    bool needs_beforeunload_check() const;

    bool operator==(WebContentPage const& other) const { return client.ptr() == other.client.ptr() && id == other.id; }
};

}
