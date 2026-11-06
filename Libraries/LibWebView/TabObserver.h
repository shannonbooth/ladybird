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

    // The frontend showing the tab deletes its widget as it hears of this.
    virtual void tab_was_removed_from_user_interface() { }

    virtual void tab_did_start_loading(URL::URL const&) { }
    virtual void tab_did_finish_loading(URL::URL const&) { }
    virtual void tab_did_change_loading_state(bool) { }
    virtual void tab_did_commit_top_level_navigation() { }
    virtual void tab_did_complete_browser_history_traversal() { }
    virtual void tab_will_begin_browser_initiated_navigation() { }
    virtual void tab_did_change_title(Utf16String const&) { }
    virtual void tab_did_change_url(URL::URL const&) { }
    virtual void tab_did_change_favicon(Optional<Gfx::Bitmap const&>) { }
    virtual void tab_did_change_indicator(TabIndicator, bool) { }
    virtual void tab_did_change_content_blocking(ContentBlockingSnapshot const&) { }
    virtual void tab_did_change_screen_wake_lock_state(Web::ScreenWakeLockState) { }
    virtual void tab_did_update_performance_stats(TabPerformanceStats const&) { }
    virtual void tab_did_find_in_page(size_t, Optional<size_t> const&) { }
    virtual void tab_did_hover_link(URL::URL const&) { }
    virtual void tab_did_unhover_link() { }
    virtual void tab_did_change_cursor(Gfx::Cursor const&) { }
    virtual void tab_did_enter_tooltip_area(ByteString const&) { }
    virtual void tab_did_leave_tooltip_area() { }
    virtual void tab_did_change_input_method_state() { }
    virtual void tab_did_change_page_background_color(Gfx::Color) { }
    virtual void tab_is_ready_to_paint() { }
    virtual void tab_did_finish_handling_key_event(Web::KeyEvent const&) { }
    virtual void tab_did_finish_handling_mouse_event(Web::MouseEvent const&, Web::EventResult) { }
    virtual void tab_did_finish_handling_drag_event(Web::DragEvent const&) { }
    virtual void tab_did_activate_picture_in_picture_control(Compositing::PictureInPictureControl) { }
    virtual void tab_web_content_did_crash(ViewImplementation::WebContentCrashReason) { }
    virtual void tab_did_change_crash_overlay_state(bool) { }
    virtual void tab_did_save_crash_report() { }
    virtual void tab_did_change_web_content_process_for_cross_site_navigation() { }

    virtual void tab_did_finish_test(String const&) { }
    virtual void tab_did_set_test_timeout(double) { }
    virtual void tab_did_receive_reference_test_metadata(JsonValue const&) { }

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
    TabObserver() = default;
    explicit TabObserver(ViewImplementation&);

    // For an observer that learns of its tab after it is made, or lets go of it before it goes.
    void observe(ViewImplementation&);
    void stop_observing();

private:
    WeakPtr<ViewImplementation> m_tab;
};

}
