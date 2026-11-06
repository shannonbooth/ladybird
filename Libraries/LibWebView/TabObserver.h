/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Noncopyable.h>
#include <AK/WeakPtr.h>
#include <LibWebView/ViewImplementation.h>

namespace WebView {

// Hears of what happens in a tab for as long as both live. Any number of observers can watch one tab.
class WEBVIEW_API TabObserver {
    AK_MAKE_NONCOPYABLE(TabObserver);
    AK_MAKE_NONMOVABLE(TabObserver);

public:
    virtual ~TabObserver();

    ViewImplementation* observed_tab() const { return m_tab.ptr(); }

    virtual void tab_did_start_loading(URL::URL const&) { }
    virtual void tab_did_finish_loading(URL::URL const&) { }

    virtual void tab_did_receive_console_message(ConsoleOutput const&) { }
    virtual void tab_did_make_source_available(Web::HTML::ScriptRegistryDescription const&) { }
    virtual void tab_did_receive_dom_mutation(Mutation const&) { }
    virtual void tab_did_receive_dom_node_properties(DOMNodeProperties const&) { }
    virtual void tab_did_receive_style_sheet_source(Web::CSS::StyleSheetIdentifier const&, URL::URL const&, Utf16String const&) { }
    virtual void tab_did_change_host_cookies(Vector<HTTP::Cookie::Cookie> const&) { }
    virtual bool wants_inspect_element_requests() const { return false; }
    virtual void tab_did_request_inspect_element(Web::UniqueNodeID) { }

    virtual void tab_did_start_network_request(u64, URL::URL const&, ByteString const&, Vector<HTTP::Header> const&, ByteBuffer const&, Optional<String> const&, String const&, bool, Web::Fetch::Infrastructure::RequestPriority) { }
    virtual void tab_did_receive_network_response_headers(u64, u32, Optional<String> const&, Vector<HTTP::Header> const&, Requests::CacheState) { }
    virtual void tab_did_receive_network_response_body(u64, ByteBuffer const&) { }
    virtual void tab_did_finish_network_request(u64, u64, Requests::RequestTimingInfo const&, Optional<Requests::NetworkError> const&) { }

    virtual void tab_debugger_did_pause(DebuggerPause const&) { }
    virtual void tab_debugger_did_resume() { }

protected:
    explicit TabObserver(ViewImplementation&);

private:
    WeakPtr<ViewImplementation> m_tab;
};

}
