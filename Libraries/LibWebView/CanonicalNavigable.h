/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/RefPtr.h>
#include <AK/String.h>
#include <LibURL/URL.h>
#include <LibWeb/HTML/ReplicatedNavigableState.h>
#include <LibWeb/PixelUnits.h>
#include <LibWebView/Export.h>
#include <LibWebView/Forward.h>

namespace WebView {

class WEBVIEW_API CanonicalNavigable {
public:
    explicit CanonicalNavigable(String id = {});
    ~CanonicalNavigable();

    enum class PendingNavigationHost : u8 {
        Local,
        Remote,
    };

    struct PendingChildFrameNavigation {
        URL::URL target_url;
        PendingNavigationHost target_host { PendingNavigationHost::Local };
    };

    String const& id() const { return m_id; }
    String const& parent_id() const { return m_parent_id; }
    void set_parent_id(String parent_id) { m_parent_id = move(parent_id); }

    Web::HTML::ReplicatedNavigableState const& replicated_state() const { return m_replicated_state; }
    Web::HTML::ReplicatedNavigableState& replicated_state() { return m_replicated_state; }

    Optional<URL::URL> const& active_document_url() const { return m_active_document_url; }
    void set_active_document_url(URL::URL const&);
    void clear_active_document_url();

    void set_active_document_host(WebContentClient&, u64 page_id);
    void clear_active_document_host();

    WebContentClient* active_document_client() { return m_active_document_client; }
    WebContentClient const* active_document_client() const { return m_active_document_client; }
    u64 active_document_page_id() const { return m_active_document_page_id; }

    void set_embedding_host(WebContentClient&, u64 page_id);
    WebContentClient* embedding_client() { return m_embedding_client; }
    WebContentClient const* embedding_client() const { return m_embedding_client; }
    u64 embedding_page_id() const { return m_embedding_page_id; }

    bool active_document_is_remote() const;
    RefPtr<WebContentClient> remote_active_document_client() const;
    u64 remote_active_document_page_id() const;

    Optional<Web::DevicePixelRect> const& viewport_rect() const { return m_viewport_rect; }
    double device_pixel_ratio() const { return m_device_pixel_ratio; }
    void set_viewport_rect(Web::DevicePixelRect, double device_pixel_ratio);

    Optional<PendingChildFrameNavigation> const& pending_child_frame_navigation() const { return m_pending_child_frame_navigation; }
    void set_pending_child_frame_navigation(URL::URL const&, PendingNavigationHost);
    void clear_pending_child_frame_navigation();

private:
    String m_id;
    String m_parent_id;
    Web::HTML::ReplicatedNavigableState m_replicated_state;
    Optional<URL::URL> m_active_document_url;
    RefPtr<WebContentClient> m_active_document_client;
    u64 m_active_document_page_id { 0 };
    RefPtr<WebContentClient> m_embedding_client;
    u64 m_embedding_page_id { 0 };
    Optional<Web::DevicePixelRect> m_viewport_rect;
    double m_device_pixel_ratio { 1 };
    Optional<PendingChildFrameNavigation> m_pending_child_frame_navigation;
};

}
