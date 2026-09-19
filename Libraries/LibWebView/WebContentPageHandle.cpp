/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/WebContentClient.h>
#include <LibWebView/WebContentPage.h>
#include <LibWebView/WebContentPageHandle.h>

namespace WebView {

WebContentPageHandle::WebContentPageHandle() = default;

WebContentPageHandle::WebContentPageHandle(RefPtr<WebContentClient> client, Web::PageId id)
    : client(move(client))
    , id(id)
{
}

WebContentClient& WebContentPageHandle::routed_connection() const
{
    return *client;
}

bool WebContentPageHandle::is_open() const
{
    return client && client->is_page_open(id);
}

WebContentPage* WebContentPageHandle::page() const
{
    return client ? client->page(id) : nullptr;
}

}
