/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Array.h>
#include <LibCore/EventLoop.h>
#include <LibWebView/Application.h>
#include <LibWebView/HeadlessWebView.h>
#include <LibWebView/TabObserver.h>

namespace Test {

class HeadlessApplication : public WebView::Application {
    WEB_VIEW_APPLICATION(HeadlessApplication)

public:
    explicit HeadlessApplication(Optional<ByteString> ladybird_binary_path);

private:
    virtual void create_platform_options(WebView::BrowserOptions&, WebView::RequestServerOptions&, WebView::WebContentOptions&) override;
    virtual Core::EventLoop& create_platform_event_loop() override { return Core::EventLoop::current(); }
    virtual bool should_coordinate_browser_process() const override { return false; }
};

class HeadlessTestEnvironment {
public:
    explicit HeadlessTestEnvironment(StringView name, unsigned timeout_seconds = 60);
    ~HeadlessTestEnvironment();

    HeadlessTestEnvironment(HeadlessTestEnvironment const&) = delete;
    HeadlessTestEnvironment& operator=(HeadlessTestEnvironment const&) = delete;

    ByteString const& directory() const { return m_directory; }

    template<typename ApplicationType = HeadlessApplication>
    ErrorOr<NonnullOwnPtr<ApplicationType>> create_application()
    {
        return ApplicationType::create({ .strings = m_arguments }, ladybird_binary_path());
    }

private:
    static Optional<ByteString> ladybird_binary_path();

    ByteString m_directory;
    Array<StringView, 1> m_arguments { "TestLibWebView"sv };
    Core::EventLoop m_event_loop;
};

// What a test does as a tab it watches tells it what happened. The test holds it for as long as it watches.
class TabEvents final : public WebView::TabObserver {
public:
    AK_ALLOC_WITH_KMALLOC;

    explicit TabEvents(WebView::ViewImplementation& tab)
        : TabObserver(tab)
    {
    }

    Function<void()> on_close;
    Function<void()> on_load_start;
    Function<void(URL::URL const&)> on_load_finish;
    Function<void()> on_browser_history_traversal_complete;
    Function<void(Utf16String const&)> on_title_change;
    Function<void(URL::URL const&)> on_url_change;
    Function<void(Optional<Gfx::Bitmap const&>)> on_favicon_change;
    Function<void(WebView::TabPerformanceStats const&)> on_performance_stats;
    Function<void(URL::URL const&)> on_link_hover;
    Function<void()> on_link_unhover;
    Function<void(Gfx::Cursor const&)> on_cursor_change;
    Function<void(ByteString const&)> on_enter_tooltip_area;
    Function<void()> on_leave_tooltip_area;
    Function<void()> on_input_method_state_change;
    Function<void()> on_ready_to_paint;
    Function<void(Compositing::PictureInPictureControl)> on_picture_in_picture_control_activated;
    Function<void(WebView::ViewImplementation::WebContentCrashReason)> on_web_content_crashed;

private:
    template<typename Callback, typename... Args>
    static void call(Callback& callback, Args&&... args)
    {
        if (callback)
            callback(forward<Args>(args)...);
    }

    virtual void tab_was_removed_from_user_interface() override { call(on_close); }
    virtual void tab_did_start_loading(URL::URL const&) override { call(on_load_start); }
    virtual void tab_did_finish_loading(URL::URL const& url) override { call(on_load_finish, url); }
    virtual void tab_did_complete_browser_history_traversal() override { call(on_browser_history_traversal_complete); }
    virtual void tab_did_change_title(Utf16String const& title) override { call(on_title_change, title); }
    virtual void tab_did_change_url(URL::URL const& url) override { call(on_url_change, url); }
    virtual void tab_did_change_favicon(Optional<Gfx::Bitmap const&> favicon) override { call(on_favicon_change, favicon); }
    virtual void tab_did_update_performance_stats(WebView::TabPerformanceStats const& stats) override { call(on_performance_stats, stats); }
    virtual void tab_did_hover_link(URL::URL const& url) override { call(on_link_hover, url); }
    virtual void tab_did_unhover_link() override { call(on_link_unhover); }
    virtual void tab_did_change_cursor(Gfx::Cursor const& cursor) override { call(on_cursor_change, cursor); }
    virtual void tab_did_enter_tooltip_area(ByteString const& tooltip) override { call(on_enter_tooltip_area, tooltip); }
    virtual void tab_did_leave_tooltip_area() override { call(on_leave_tooltip_area); }
    virtual void tab_did_change_input_method_state() override { call(on_input_method_state_change); }
    virtual void tab_is_ready_to_paint() override { call(on_ready_to_paint); }
    virtual void tab_did_activate_picture_in_picture_control(Compositing::PictureInPictureControl control) override { call(on_picture_in_picture_control_activated, control); }
    virtual void tab_web_content_did_crash(WebView::ViewImplementation::WebContentCrashReason reason) override { call(on_web_content_crashed, reason); }
};

// A headless window whose test answers some of what its tab asks of it, as a frontend would.
class RequestHandlingWebView final : public WebView::HeadlessWebView {
public:
    static NonnullOwnPtr<RequestHandlingWebView> create(Core::AnonymousBuffer theme, Web::DevicePixelSize window_size, WebView::IsPrivate is_private = WebView::IsPrivate::No)
    {
        auto web_view = adopt_own(*new RequestHandlingWebView(move(theme), window_size, open_tab(is_private)));
        web_view->tab().activate();
        return web_view;
    }

    Function<String(Web::HTML::ActivateTab, Web::HTML::WebViewHints, WebView::CanonicalTraversable&)> on_new_web_view;
    Function<void(Utf16String const&, NonnullRefPtr<WebView::DialogReply>)> on_request_alert;
    Function<void(String const&, NonnullRefPtr<WebView::DialogReply>)> on_request_before_unload;
    Function<void(Color, NonnullRefPtr<WebView::ColorPickerReply>)> on_request_color_picker;
    Function<void()> on_request_website_issue_report;

    // What a headless window does with a tab or window a page opened.
    String show_tab_opened_by_page_as_headless(Web::HTML::ActivateTab activate_tab, Web::HTML::WebViewHints hints, WebView::CanonicalTraversable& traversable)
    {
        return HeadlessWebView::show_tab_opened_by_page(activate_tab, hints, traversable);
    }

private:
    using HeadlessWebView::HeadlessWebView;

    virtual String show_tab_opened_by_page(Web::HTML::ActivateTab activate_tab, Web::HTML::WebViewHints hints, WebView::CanonicalTraversable& traversable) override
    {
        if (on_new_web_view)
            return on_new_web_view(activate_tab, hints, traversable);
        return HeadlessWebView::show_tab_opened_by_page(activate_tab, hints, traversable);
    }
    virtual void request_alert(Utf16String const& message, NonnullRefPtr<WebView::DialogReply> reply) override
    {
        if (on_request_alert)
            on_request_alert(message, move(reply));
        else
            HeadlessWebView::request_alert(message, move(reply));
    }
    virtual void request_before_unload(String const& source, NonnullRefPtr<WebView::DialogReply> reply) override
    {
        if (on_request_before_unload)
            on_request_before_unload(source, move(reply));
        else
            HeadlessWebView::request_before_unload(source, move(reply));
    }
    virtual void request_color_picker(Color color, NonnullRefPtr<WebView::ColorPickerReply> reply) override
    {
        if (on_request_color_picker)
            on_request_color_picker(color, move(reply));
        else
            HeadlessWebView::request_color_picker(color, move(reply));
    }
    virtual void request_website_issue_report() override
    {
        if (on_request_website_issue_report)
            on_request_website_issue_report();
    }
};

}
