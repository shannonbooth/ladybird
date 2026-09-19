/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullRefPtr.h>
#include <LibWeb/Page/PageId.h>
#include <LibWebView/Export.h>
#include <LibWebView/Forward.h>
#include <WebContent/WebContentServerEndpoint.h>

namespace WebView {

// Names a page a WebContent process holds for the UI process: its client and its id. A handle is a value that
// outlives the page it names, so the canonical tree can record which page hosts, reported or asked for something.
class WEBVIEW_API WebContentPageHandle : public WebContentServerPageProxy<WebContentPageHandle> {
public:
    WebContentPageHandle(WebContentClient&, Web::PageId);
    WebContentPageHandle(WebContentPageHandle const&);
    WebContentPageHandle(WebContentPageHandle&&);
    WebContentPageHandle& operator=(WebContentPageHandle const&);
    WebContentPageHandle& operator=(WebContentPageHandle&&);
    ~WebContentPageHandle();

    WebContentClient& client() const { return *m_client; }
    Web::PageId id() const { return m_id; }

    // What the bound proxy sends through, and about.
    WebContentClient& routed_connection() const { return *m_client; }
    Web::PageId routed_page_id() const { return m_id; }

    // Whether the page is open in a process that is still running.
    bool is_open() const;
    // The open page this names, resolved by its client. Null once the page closed. A crashed process's pages stay
    // open until their tabs release what they hosted.
    WebContentPage* page() const;

    bool operator==(WebContentPageHandle const& other) const { return m_client.ptr() == other.m_client.ptr() && m_id == other.m_id; }

private:
    NonnullRefPtr<WebContentClient> m_client;
    Web::PageId m_id;
};

}
