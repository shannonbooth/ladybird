/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/ByteString.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/Utf16String.h>
#include <AK/Vector.h>
#include <LibGfx/Color.h>
#include <LibGfx/Cursor.h>
#include <LibGfx/Point.h>
#include <LibGfx/Rect.h>
#include <LibGfx/ShareableBitmap.h>
#include <LibGfx/SharedImage.h>
#include <LibGfx/Size.h>
#include <LibHTTP/Header.h>
#include <LibRequests/CacheState.h>
#include <LibRequests/NetworkError.h>
#include <LibRequests/RequestTimingInfo.h>
#include <LibURL/Origin.h>
#include <LibURL/URL.h>
#include <LibWebCommon/CSS/StyleSheetIdentifier.h>
#include <LibWebCommon/Clipboard/SystemClipboard.h>
#include <LibWebCommon/Fetch/Infrastructure/HTTP/RequestPriority.h>
#include <LibWebCommon/HTML/ActivateTab.h>
#include <LibWebCommon/HTML/CrossProcessId.h>
#include <LibWebCommon/HTML/FileFilter.h>
#include <LibWebCommon/HTML/PreparedNavigationDescriptor.h>
#include <LibWebCommon/HTML/Scripting/ScriptRegistryTypes.h>
#include <LibWebCommon/HTML/SelectItem.h>
#include <LibWebCommon/HTML/SessionHistoryEntryDescriptor.h>
#include <LibWebCommon/HTML/VisibilityState.h>
#include <LibWebCommon/HTML/WebViewHints.h>
#include <LibWebCommon/Page/ContextMenuForInputEventsTarget.h>
#include <LibWebCommon/Page/EventResult.h>
#include <LibWebCommon/Page/InputEvent.h>
#include <LibWebCommon/Page/MediaContextMenu.h>
#include <LibWebCommon/Page/ScreenWakeLockState.h>
#include <LibWebCommon/Page/SpellingError.h>
#include <LibWebCommon/Page/ViewportIsFullscreen.h>
#include <LibWebCommon/WebDriver/Response.h>
#include <LibWebCommon/WebView/AccessibilityNodeData.h>
#include <LibWebCommon/WebView/ConsoleOutput.h>
#include <LibWebCommon/WebView/DOMNodeProperties.h>
#include <LibWebCommon/WebView/Debugger.h>
#include <LibWebCommon/WebView/Mutation.h>
#include <LibWebView/CanonicalTraversable.h>
#include <LibWebView/Export.h>
#include <LibWebView/Forward.h>
#include <LibWebView/HistoryVisitTransition.h>
#include <LibWebView/MicrophoneAuthorizer.h>
#include <LibWebView/PendingReply.h>

namespace WebView {

// What a tab's pages ask of the user and the browser on the user's behalf. Without a view, nothing is shown and every
// request gets its no-UI answer.
class WEBVIEW_API TabUserRequests {
public:
    virtual ~TabUserRequests() = default;

    virtual bool has_open_dialog() const { return false; }
    virtual void request_alert(Utf16String const& message, NonnullRefPtr<DialogReply>);
    virtual void request_confirm(Utf16String const& message, NonnullRefPtr<DialogReply>);
    virtual void request_prompt(Utf16String const& message, Utf16String const& default_value, NonnullRefPtr<DialogReply>);
    virtual void request_before_unload(String const& source, NonnullRefPtr<DialogReply>);
    virtual void did_request_set_prompt_text(Utf16String const&) { }
    virtual void did_request_accept_dialog() { }
    virtual void did_request_dismiss_dialog() { }
    virtual void request_color_picker(Color, NonnullRefPtr<ColorPickerReply>);
    virtual void request_date_picker(Utf16String const& input_type, Utf16String const& value, Utf16String const& minimum, Utf16String const& maximum, NonnullRefPtr<DatePickerReply>);
    virtual void request_file_picker(Web::HTML::FileFilter const&, Web::HTML::AllowMultipleFiles, NonnullRefPtr<FilePickerReply>);
    virtual void request_select_dropdown(Gfx::IntPoint content_position, i32 minimum_width, Vector<Web::HTML::SelectItem>, NonnullRefPtr<SelectDropdownReply>);
    virtual void did_request_permission(Web::Permissions::PermissionName) { }
    virtual void request_permission_prompt(PermissionPromptRequest, NonnullRefPtr<PermissionPromptReply>);
    virtual MicrophoneAuthorization microphone_authorization() const { return MicrophoneAuthorization::Denied; }
    virtual void ensure_microphone_authorization(NonnullRefPtr<MicrophoneAuthorizationReply>);
    virtual void show_microphone_denied_notice() { }
    virtual void request_geolocation_position(WebContentPage&, u64 request_id, Web::HTML::CrossProcessId document_id);
    virtual void cancel_geolocation_position_request(WebContentPage&, u64) { }
    virtual void start_geolocation_position_watch(WebContentPage&, u64 request_id, Web::HTML::CrossProcessId document_id);
    virtual void stop_geolocation_position_watch(WebContentPage&, u64) { }
    virtual void request_spell_check(Utf16String const& text, NonnullRefPtr<SpellCheckReply>);
    virtual void request_writing_suggestion(Utf16String const& text, u32 caret_offset, NonnullRefPtr<WritingSuggestionReply>);
    virtual Web::Clipboard::SystemClipboardItem clipboard_item() const { return {}; }
    virtual void insert_clipboard_item(Web::Clipboard::SystemClipboardItem) { }
    virtual void did_request_page_context_menu(WebContentPage&, Gfx::IntPoint, Web::ContextMenuForInputEventsTarget, Optional<Utf16String>, Optional<u64>) { }
    virtual void did_request_link_context_menu(Gfx::IntPoint, Web::HTML::PreparedNavigationDescriptor, Optional<u64>) { }
    virtual void did_request_image_context_menu(Gfx::IntPoint, Web::HTML::PreparedNavigationDescriptor, Optional<Gfx::ShareableBitmap>) { }
    virtual void did_request_media_context_menu(WebContentPage&, Gfx::IntPoint, Web::MediaContextMenu, Optional<Web::HTML::PreparedNavigationDescriptor>) { }

    // The window.
    virtual void restore_window() { }
    virtual void reposition_window(Gfx::IntPoint) { }
    virtual void resize_window(Gfx::IntSize) { }
    virtual void maximize_window() { }
    virtual void minimize_window() { }
    virtual void request_fullscreen_window(WebContentPage&);
    virtual void exit_fullscreen_window() { }
    virtual void fully_exit_fullscreen() { }
    virtual Web::ViewportIsFullscreen viewport_is_fullscreen() const { return Web::ViewportIsFullscreen::No; }
    virtual void set_system_visibility_state(Web::HTML::VisibilityState) { }
    virtual void send_preferences_to_page(WebContentPage&) { }

    // Navigations the user started in a page.
    virtual void did_request_refresh() { }
    virtual void did_request_external_url(URL::URL, URL::Origin, bool) { }
    virtual bool take_input_to_open_link_in_new_tab() { return false; }
    virtual void did_request_link_navigation(Web::HTML::PreparedNavigationDescriptor) { }
    virtual void did_request_link_navigation_in_new_tab(Web::HTML::PreparedNavigationDescriptor, Web::HTML::ActivateTab) { }
    virtual String did_request_new_web_view(Web::HTML::ActivateTab, Web::HTML::WebViewHints, CanonicalTraversable&) { return {}; }
    virtual void did_close_browsing_context() { }
};

// What a tab's pages tell whoever displays the tab: its document, the view's surface and the input sent through it.
// Without a view, nothing is displayed and these are dropped.
class WEBVIEW_API TabDisplay {
public:
    virtual ~TabDisplay() = default;

    virtual HistoryVisitTransition history_visit_transition_for_current_load() const { return HistoryVisitTransition::Link; }
    virtual void did_begin_top_level_load(URL::URL const&) { }
    virtual void did_finish_top_level_load(URL::URL const&) { }
    virtual void did_find_in_page(size_t, Optional<size_t>) { }
    virtual void did_change_picture_in_picture_player_controls_visibility(bool) { }

    // The view's surface and input.
    struct InputMethodState {
        bool is_enabled { false };
        i32 cursor_position { 0 };
        i32 anchor_position { 0 };
        Utf16String text_before_cursor;
        Utf16String text_after_cursor;
        Optional<Web::DevicePixelRect> caret_rect;
    };
    virtual void server_did_paint(i32, Gfx::IntSize, Gfx::IntRect, double) { }
    virtual void did_allocate_backing_stores(Vector<i32>, Vector<Gfx::SharedImage>) { }
    virtual void did_add_backing_stores(Vector<i32>, Vector<Gfx::SharedImage>) { }
    virtual void did_retire_backing_stores(ReadonlySpan<i32>) { }
    virtual void did_consume_input_event_in_compositor(u64) { }
    virtual void did_not_dispatch_input_event_through_compositor(u64) { }
    virtual void did_dispatch_input_event_through_compositor(u64, bool) { }
    virtual void discard_input_events_routed_through_lost_compositor() { }
    virtual void did_finish_handling_input_event(WebContentPage const&, u64, Web::EventResult) { }
    virtual void did_forward_input_event(WebContentPage const&, u64, WebContentPage&) { }
    virtual void did_forward_pinch_event(WebContentPage const&, Web::PinchEvent const&, WebContentPage&) { }
    virtual void did_lose_input_event_endpoint(WebContentPage&) { }
    virtual bool is_delivering_user_activation_to(WebContentPage const&) const { return false; }
    virtual void consume_user_activation_of_input_in_flight_to(WebContentPage const&) { }
    virtual void consume_user_activation_of_input_handled_by(WebContentPage const&) { }
    virtual void did_request_cursor_change(Gfx::Cursor) { }
    virtual void set_input_method_state(InputMethodState) { }
    virtual void set_editing_history_state(bool, bool) { }
    virtual void did_request_tooltip_override(Gfx::IntPoint, ByteString const&) { }
    virtual void did_stop_tooltip_override() { }
    virtual void did_enter_tooltip_area(ByteString) { }
    virtual void did_leave_tooltip_area() { }
    virtual void did_hover_link(URL::URL const&) { }
    virtual void did_unhover_link() { }
    virtual void did_set_browser_zoom(double) { }
    virtual void did_get_accessibility_tree(Vector<AccessibilityNodeData>) { }
    virtual void did_change_accessibility_focus(i64) { }
};

// What a tab's pages tell the tools that drive and inspect the tab: DevTools, WebDriver and tests.
class WEBVIEW_API TabAutomation {
public:
    virtual ~TabAutomation() = default;

    virtual void did_get_style_sheet_source(Web::CSS::StyleSheetIdentifier const&, URL::URL const&, Utf16String const&) { }
    virtual void did_add_devtools_source(Web::HTML::ScriptRegistryDescription) { }
    virtual void did_pause_debugger(DebuggerPause) { }
    virtual void did_resume_debugger() { }
    virtual void did_output_console_message(ConsoleOutput) { }
    virtual void did_start_network_request(u64, URL::URL const&, ByteString const&, Vector<HTTP::Header> const&, ByteBuffer, Optional<String>, String, bool, Web::Fetch::Infrastructure::RequestPriority) { }
    virtual void did_receive_network_response_headers(u64, u32, Optional<String> const&, Vector<HTTP::Header> const&, Requests::CacheState) { }
    virtual void did_receive_network_response_body(u64, ByteBuffer) { }
    virtual void did_finish_network_request(u64, u64, Requests::RequestTimingInfo const&, Optional<Requests::NetworkError> const&) { }
    virtual void did_inspect_dom_node(DOMNodeProperties) { }
    virtual void did_mutate_dom(Mutation) { }
    virtual void did_update_indexed_database(JsonObject) { }

    virtual void did_set_webdriver_current_browsing_context(u64, Web::HTML::CrossProcessId) { }
    virtual void did_complete_webdriver_content_command(WebContentPage&, u64, Web::WebDriver::Response) { }
    virtual void enqueue_webdriver_mouse_event(Web::MouseEvent, Function<void()> on_handled) { on_handled(); }
    virtual void enqueue_key_event_for_testing(Web::KeyEvent) { }
    virtual void enqueue_pinch_event_for_testing(Web::PinchEvent) { }
    virtual void set_device_pixel_ratio_for_testing(WebContentPage&, double) { }
    virtual void activate_spelling_menu_action_for_testing(WebContentPage&, Web::SpellingMenuActionForTesting, u32) { }
    virtual void set_next_permission_prompt_outcome_for_testing(Web::Permissions::PermissionName, Web::Permissions::PromptOutcome, bool) { }
    virtual void release_held_permission_prompt_for_testing(u32) { }
    virtual void set_microphone_authorization_for_testing(MicrophoneAuthorization, MicrophoneAuthorization, bool) { }
    virtual void release_system_microphone_answer_for_testing() { }
    virtual u64 system_microphone_requests_for_testing() const { return 0; }
    virtual u64 microphone_denied_notices_shown_for_testing() const { return 0; }
    virtual void did_finish_test(String const&) { }
    virtual void did_set_test_timeout(double) { }
    virtual void did_receive_reference_test_metadata(JsonValue const&) { }
    virtual void did_request_stop_loading_for_testing() { }
    virtual void did_request_reload_for_testing(BypassCache) { }
    virtual void did_request_traverse_history_by_delta_for_testing(int) { }
    virtual void did_reset_state_for_testing(Web::HTML::SessionHistoryEntryDescriptor) { }
    virtual String ui_process_session_history_for_testing() const { return "{}"_string; }
    virtual bool capture_session_history_snapshot_for_testing() { return false; }
    virtual bool restore_captured_session_history_snapshot_for_testing() { return false; }
    virtual bool register_session_store_tab_for_testing() { return false; }
    virtual String session_store_tab_state_for_testing() const { return "{}"_string; }
};

// Everything a tab's pages may tell or ask whoever shows the tab. The view overrides it. A tab no view shows has the
// base behaviour, which shows nothing.
class WEBVIEW_API TabUI
    : public TabUserRequests
    , public TabDisplay
    , public TabAutomation {
public:
    virtual ~TabUI() override = default;

    static TabUI& no_ui();
};

}
