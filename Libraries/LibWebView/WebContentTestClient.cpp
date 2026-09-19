/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/CookieJar.h>
#include <LibWebView/WebContentClient.h>
#include <LibWebView/WebContentPage.h>
#include <LibWebView/WebContentTestClient.h>

namespace WebView {

WebContentTestClient::WebContentTestClient(NonnullOwnPtr<IPC::Transport> transport, WebContentClient& client)
    : WebContentTestClientPageRoutingStub(*this, move(transport))
    , m_client(client)
{
}

WebContentTestClient::~WebContentTestClient() = default;

// This transport is separate, so dispatch what the process sent on its main connection before anything from here.
ErrorOr<OwnPtr<IPC::MessageBuffer>> WebContentTestClient::handle(NonnullOwnPtr<IPC::Message> message)
{
    m_client.dispatch_pending_messages();
    return WebContentTestClientEndpoint::Stub::handle(move(message));
}

void WebContentTestClient::die()
{
    // The WebContent process going away is handled by the main connection.
}

WebContentTestClientPageStub* WebContentTestClient::page_stub(Web::PageId const& page_id)
{
    return m_client.page(page_id);
}

void WebContentTestClient::did_expire_cookies_with_time_offset(AK::Duration offset)
{
    m_client.session().cookie_jar->expire_cookies_with_time_offset(offset);
}

}
