/*
 * Copyright (c) 2024-2025, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "TestWeb.h"

#include <AK/Badge.h>
#include <AK/RefPtr.h>
#include <LibCore/Forward.h>
#include <LibCore/Promise.h>
#include <LibGfx/Forward.h>
#include <LibWebCommon/PixelUnits.h>
#include <LibWebView/HeadlessWebView.h>

namespace TestWeb {

class TestWebView final : public WebView::HeadlessWebView {
public:
    virtual ~TestWebView() override = default;

    static NonnullOwnPtr<TestWebView> create(Core::AnonymousBuffer theme, Web::DevicePixelSize window_size);

    void clear_content_blockers();
    void reset_force_dark();
    void reset_line_box_borders();
    void reset_user_style();
    void reset_geolocation_emulated_position();
    void reset_permissions();
    void reset_notifications();
    NonnullRefPtr<Core::Promise<Empty>> reset_state_for_next_test();

    NonnullRefPtr<Core::Promise<RefPtr<Gfx::Bitmap const>>> take_screenshot();

    TestPromise& test_promise() { return *m_test_promise; }
    void reset_test_promise() { m_test_promise = TestPromise::construct(); }
    void on_test_complete(TestCompletion);

    // What the runner does as the test it runs makes progress.
    Function<void(URL::URL const&)> on_load_finish;
    Function<void(String const&)> on_test_finish;
    Function<void(double milliseconds)> on_set_test_timeout;
    Function<void(JsonValue const&)> on_reference_test_metadata;
    Function<void(WebView::ViewImplementation::WebContentCrashReason)> on_web_content_crashed;
    Function<void()> on_web_content_process_change_for_cross_site_navigation;

private:
    TestWebView(Core::AnonymousBuffer theme, Web::DevicePixelSize viewport_size, WebView::ViewImplementation&);

    // ^WebView::TabObserver
    virtual void tab_did_finish_loading(URL::URL const& url) override
    {
        if (on_load_finish)
            on_load_finish(url);
    }
    virtual void tab_did_finish_test(String const& text) override
    {
        if (on_test_finish)
            on_test_finish(text);
    }
    virtual void tab_did_set_test_timeout(double milliseconds) override
    {
        if (on_set_test_timeout)
            on_set_test_timeout(milliseconds);
    }
    virtual void tab_did_receive_reference_test_metadata(JsonValue const& metadata) override
    {
        if (on_reference_test_metadata)
            on_reference_test_metadata(metadata);
    }
    virtual void tab_web_content_did_crash(WebView::ViewImplementation::WebContentCrashReason crash_reason) override
    {
        HeadlessWebView::tab_web_content_did_crash(crash_reason);
        if (on_web_content_crashed)
            on_web_content_crashed(crash_reason);
    }
    virtual void tab_did_change_web_content_process_for_cross_site_navigation() override
    {
        if (on_web_content_process_change_for_cross_site_navigation)
            on_web_content_process_change_for_cross_site_navigation();
    }

    virtual Web::Clipboard::SystemClipboardItem clipboard_item() const override { return m_clipboard_item; }
    virtual void insert_clipboard_item(Web::Clipboard::SystemClipboardItem item) override { m_clipboard_item = move(item); }

    RefPtr<Core::Promise<RefPtr<Gfx::Bitmap const>>> m_pending_screenshot;

    Web::Clipboard::SystemClipboardItem m_clipboard_item;

    NonnullRefPtr<TestPromise> m_test_promise;
};

}
