/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/CanonicalNavigable.h>
#include <LibWebView/WebContentClient.h>

namespace WebView {

CanonicalNavigable::CanonicalNavigable(String id)
    : m_id(move(id))
{
}

CanonicalNavigable::~CanonicalNavigable() = default;

void CanonicalNavigable::set_active_document_url(URL::URL const& url)
{
    m_active_document_url = url;
}

void CanonicalNavigable::clear_active_document_url()
{
    m_active_document_url.clear();
}

void CanonicalNavigable::set_active_document_host(WebContentClient& client, u64 page_id)
{
    m_active_document_client = client;
    m_active_document_page_id = page_id;
}

void CanonicalNavigable::clear_active_document_host()
{
    m_active_document_client = nullptr;
    m_active_document_page_id = 0;
}

void CanonicalNavigable::set_embedding_host(WebContentClient& client, u64 page_id)
{
    m_embedding_client = client;
    m_embedding_page_id = page_id;
}

bool CanonicalNavigable::active_document_is_remote() const
{
    return m_active_document_client
        && (m_active_document_client != m_embedding_client
            || m_active_document_page_id != m_embedding_page_id);
}

RefPtr<WebContentClient> CanonicalNavigable::remote_active_document_client() const
{
    if (!active_document_is_remote())
        return nullptr;
    return m_active_document_client;
}

u64 CanonicalNavigable::remote_active_document_page_id() const
{
    if (!active_document_is_remote())
        return 0;
    return m_active_document_page_id;
}

void CanonicalNavigable::set_viewport_rect(Web::DevicePixelRect viewport_rect, double device_pixel_ratio)
{
    m_viewport_rect = viewport_rect;
    m_device_pixel_ratio = device_pixel_ratio;
}

void CanonicalNavigable::set_pending_child_frame_navigation(URL::URL const& url, PendingNavigationHost target_host)
{
    m_pending_child_frame_navigation = PendingChildFrameNavigation {
        .target_url = url,
        .target_host = target_host,
    };
}

void CanonicalNavigable::clear_pending_child_frame_navigation()
{
    m_pending_child_frame_navigation.clear();
}

}
