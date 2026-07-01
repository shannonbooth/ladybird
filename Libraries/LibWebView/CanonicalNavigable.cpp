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

RefPtr<WebContentClient> CanonicalNavigable::active_document_client_handle() const
{
    return m_active_document_client;
}

}
