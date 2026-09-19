/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibIPC/ConnectionToServer.h>
#include <LibWeb/Page/PageId.h>
#include <LibWebView/Forward.h>
#include <WebContent/WebContentTestClientEndpoint.h>
#include <WebContent/WebContentTestServerEndpoint.h>

namespace WebView {

// The test-only half of the WebContent protocol. The UI process connects this second transport only when it runs
// tests, so a WebContent process serving a real browsing session cannot send these messages at all.
class WEBVIEW_API WebContentTestClient final
    : public WebContentTestClientPageRoutingStub<IPC::ConnectionToServer<WebContentTestClientEndpoint, WebContentTestServerEndpoint>>
    , public WebContentTestClientEndpoint {
    C_OBJECT_ABSTRACT(WebContentTestClient);

public:
    WebContentTestClient(NonnullOwnPtr<IPC::Transport>, WebContentClient&);
    ~WebContentTestClient();

private:
    virtual ErrorOr<OwnPtr<IPC::MessageBuffer>> handle(NonnullOwnPtr<IPC::Message>) override;

    virtual void die() override;

    // A page's test messages reach the page, once the main connection's client has found it open.
    virtual WebContentTestClientPageStub* page_stub(Web::PageId const&) override;
    virtual void did_expire_cookies_with_time_offset(AK::Duration) override;

    WebContentClient& m_client;
};

}
