/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/WebContentClient.h>
#include <LibWebView/WebContentPage.h>
#include <LibWebView/WebContentPageHandle.h>

namespace WebView {

WebContentPageHandle::WebContentPageHandle(WebContentClient& client, Web::PageId id)
    : m_client(client)
    , m_id(id)
{
}

WebContentPageHandle::WebContentPageHandle(WebContentPageHandle const&) = default;
WebContentPageHandle::WebContentPageHandle(WebContentPageHandle&&) = default;
WebContentPageHandle& WebContentPageHandle::operator=(WebContentPageHandle const&) = default;
WebContentPageHandle& WebContentPageHandle::operator=(WebContentPageHandle&&) = default;
WebContentPageHandle::~WebContentPageHandle() = default;

bool WebContentPageHandle::is_open() const
{
    return m_client->is_page_open(m_id);
}

WebContentPage* WebContentPageHandle::page() const
{
    return m_client->page(m_id);
}

}
