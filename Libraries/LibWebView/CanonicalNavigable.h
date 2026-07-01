/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/RefPtr.h>
#include <AK/String.h>
#include <LibWeb/HTML/ReplicatedNavigableState.h>
#include <LibWebView/Export.h>
#include <LibWebView/Forward.h>

namespace WebView {

class WEBVIEW_API CanonicalNavigable {
public:
    explicit CanonicalNavigable(String id = {});
    ~CanonicalNavigable();

    String const& id() const { return m_id; }

    Web::HTML::ReplicatedNavigableState const& replicated_state() const { return m_replicated_state; }
    Web::HTML::ReplicatedNavigableState& replicated_state() { return m_replicated_state; }

    void set_active_document_host(WebContentClient&, u64 page_id);
    void clear_active_document_host();

    WebContentClient* active_document_client() { return m_active_document_client; }
    WebContentClient const* active_document_client() const { return m_active_document_client; }
    u64 active_document_page_id() const { return m_active_document_page_id; }

private:
    String m_id;
    Web::HTML::ReplicatedNavigableState m_replicated_state;
    RefPtr<WebContentClient> m_active_document_client;
    u64 m_active_document_page_id { 0 };
};

}
