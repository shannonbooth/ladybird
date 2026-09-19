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
#include <WebContent/WebContentServerEndpoint.h>

namespace WebView {

// Names a page a WebContent process holds for the UI process: its client and its id. A handle is a value that
// outlives the page it names; page() resolves the open page, and is null once the page closed.
struct WEBVIEW_API WebContentPageHandle : public WebContentServerPageProxy<WebContentPageHandle> {
    RefPtr<WebContentClient> client;
    Web::PageId id { 0 };

    WebContentPageHandle();
    WebContentPageHandle(RefPtr<WebContentClient>, Web::PageId);

    // What the bound proxy sends through, and about.
    WebContentClient& routed_connection() const;
    Web::PageId routed_page_id() const { return id; }

    // Whether the page is open in a process that is still running.
    bool is_open() const;
    // The open page this names, resolved by its client. Null once the page closed. A crashed process's pages stay
    // open until their tabs release what they hosted.
    WebContentPage* page() const;

    bool operator==(WebContentPageHandle const& other) const { return client.ptr() == other.client.ptr() && id == other.id; }
};

}
