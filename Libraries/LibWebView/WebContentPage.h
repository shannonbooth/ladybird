/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/RefPtr.h>
#include <LibWeb/HTML/CrossProcessId.h>
#include <LibWeb/Page/PageId.h>
#include <LibWebView/Export.h>
#include <LibWebView/Forward.h>
#include <WebContent/WebContentClientEndpoint.h>

namespace WebView {

// A page a WebContent process holds for the UI process. It holds the graph of one tab, and displays that tab when it
// is the view's page.
struct WEBVIEW_API WebContentPage {
    RefPtr<WebContentClient> client;
    Web::PageId id { 0 };

    bool is_open() const;
    CanonicalTraversable* traversable() const;
    // The view when this page displays its tab.
    Optional<ViewImplementation&> view() const;
    // The view of the tab this page holds, whichever page displays it.
    Optional<ViewImplementation&> owning_view() const;
    Optional<CanonicalNavigable&> hosted_navigable(Web::HTML::CrossProcessId) const;
    bool needs_beforeunload_check() const;
    Optional<WebContentPage> endpoint_hosting_navigable_represented_by(CanonicalTraversable&, Web::HTML::CrossProcessId navigable_id) const;

    // Handlers for messages the page's process sends about it. The client forwards each message here.
    void did_request_navigation_of_navigable(Web::HTML::CrossProcessId navigable_id, Web::HTML::PreparedNavigationDescriptor navigation);
    void did_post_message_to_navigable(Web::HTML::CrossProcessId navigable_id, Web::HTML::PostedMessageDescriptor message);
    void did_request_focusing_steps_for_navigable(Web::HTML::CrossProcessId navigable_id, Web::HTML::FocusTrigger focus_trigger);
    void did_request_window_focus_of_navigable(Web::HTML::CrossProcessId navigable_id);
    void did_request_set_opener_of_navigable(Web::HTML::CrossProcessId navigable_id, Web::HTML::CrossProcessId opener_navigable_id);
    void did_completely_finish_loading(Web::HTML::CrossProcessId navigable_id);
    void did_create_child_frame(Web::HTML::CrossProcessId parent_frame_id, Web::HTML::CrossProcessId frame_id, Web::HTML::ReplicatedNavigableState replicated_state);
    void did_set_browser_zoom(double factor);
    void did_find_in_page(size_t current_match_index, Optional<size_t> total_match_count);
    void did_request_refresh();
    void did_request_cursor_change(Gfx::Cursor cursor);
    void did_update_editing_history_state(bool can_undo, bool can_redo);
    void did_request_tooltip_override(Gfx::IntPoint position, ByteString title);
    void did_stop_tooltip_override();
    void did_enter_tooltip_area(ByteString title);
    void did_leave_tooltip_area();
    void did_hover_link(URL::URL url);
    void did_unhover_link();
    void did_click_link(URL::URL url, ByteString target, unsigned modifiers);
    void did_middle_click_link(URL::URL url, ByteString, unsigned);
    void did_request_external_url(URL::URL url, URL::Origin initiator_origin, bool has_transient_activation);
    void did_inspect_storage(u64 request_id, String storage_items);
    void did_inspect_grid_layouts(String grid_layouts);
    void did_inspect_current_grid(String grid_layout);
    void did_inspect_current_flexbox(String flexbox_layout);
    void did_inspect_indexed_database(u64 request_id, String result);
    void did_inspect_accessibility_tree(String accessibility_tree);
    void did_get_hovered_node_id(Web::UniqueNodeID node_id);
    void did_get_node_id_at_position(u64 request_id, Web::UniqueNodeID node_id);
    void did_list_style_sheets(Vector<Web::CSS::StyleSheetIdentifier> stylesheets);
    void did_get_style_sheet_source(Web::CSS::StyleSheetIdentifier identifier, URL::URL base_url, Utf16String source);
    void did_list_devtools_sources(u64 request_id, Vector<Web::HTML::ScriptRegistry::Description> sources);
    void did_get_devtools_source(Web::HTML::ScriptRegistry::Identifier source_id, Optional<Web::HTML::ScriptRegistry::Content> source);
    void did_add_devtools_source(Web::HTML::ScriptRegistry::Description source);
    void did_pause_debugger(DebuggerPause pause);
    void did_resume_debugger();
    void did_complete_debugger_breakpoint_operation(u64 request_id, Optional<String> error);
    void did_take_screenshot(Gfx::ShareableBitmap screenshot);
    void did_get_internal_page_info(WebView::PageInfoType type, Optional<Core::AnonymousBuffer> info);
    void did_get_selected_text(u64 request_id, ByteString selection);
    void did_get_selected_text_for_lookup(u64 request_id, Optional<DictionaryLookup> lookup);
    void did_select_word_for_dictionary_lookup(u64 request_id, bool selected);
    void did_cut_selected_text(u64 request_id, ByteString selection);
    void did_execute_js_console_input(JsonValue result);
    void did_output_js_console_message(ConsoleOutput console_output);
    void did_start_network_request(u64 request_id, URL::URL url, ByteString method, Vector<HTTP::Header> request_headers, ByteBuffer request_body, Optional<String> initiator_type, String referrer_policy, bool is_navigation_request, Web::Fetch::Infrastructure::Request::Priority priority);
    void did_receive_network_response_body(u64 request_id, ByteBuffer data);
    void did_finish_network_request(u64 request_id, u64 body_size, Requests::RequestTimingInfo timing_info, Optional<Requests::NetworkError> network_error);
    void did_request_set_prompt_text(Utf16String message);
    void did_request_accept_dialog();
    void did_request_dismiss_dialog();
    void did_request_document_cookie_version_index(i64 document_id, String domain);
    Messages::WebContentClient::DidRequestStorageItemResponse did_request_storage_item(Web::StorageAPI::StorageEndpointType storage_endpoint, String storage_key, Utf16String bottle_key);
    Messages::WebContentClient::DidSetStorageItemResponse did_set_storage_item(Web::StorageAPI::StorageEndpointType storage_endpoint, String storage_key, Utf16String bottle_key, Utf16String value);
    void did_remove_storage_item(Web::StorageAPI::StorageEndpointType storage_endpoint, String storage_key, Utf16String bottle_key);
    Messages::WebContentClient::DidRequestStorageKeysResponse did_request_storage_keys(Web::StorageAPI::StorageEndpointType storage_endpoint, String storage_key);
    void did_clear_storage(Web::StorageAPI::StorageEndpointType storage_endpoint, String storage_key);
    void did_request_activate_tab();
    void did_change_needs_beforeunload_check(bool needs_beforeunload_check);
    void did_consume_user_activation(Web::HTML::UserActivationConsumption consumption);
    void webdriver_user_prompt_handling_complete(u64 request_id, Web::WebDriver::Response response);
    void webdriver_did_set_current_browsing_context(u64 command_id, Web::HTML::CrossProcessId navigable_id);
    void webdriver_command_complete(u64 command_id, Web::WebDriver::Response response);
    void did_update_resource_count(i32 count_waiting);
    void did_request_restore_window();
    void did_request_reposition_window(Gfx::IntPoint position, u64 completion_id);
    void did_request_resize_window(Gfx::IntSize size, u64 completion_id);
    void did_request_maximize_window(u64 completion_id);
    void did_request_minimize_window();
    void did_request_fullscreen_window();
    void did_request_exit_fullscreen();
    void did_request_file(ByteString path, i32 request_id);
    void did_request_color_picker(Color current_color);
    void did_request_geolocation_position(u64 request_id);
    void did_cancel_geolocation_position_request(u64 request_id);
    void did_start_geolocation_position_watch(u64 request_id);
    void did_stop_geolocation_position_watch(u64 request_id);
    void did_request_file_picker(Web::HTML::FileFilter accepted_file_types, Web::HTML::AllowMultipleFiles allow_multiple_files);
    void did_finish_handling_input_event(u64 event_id, Web::EventResult event_result);
    void did_update_input_method_state(Optional<Web::DevicePixelRect> caret_rect, bool is_enabled, i32 cursor_position, i32 anchor_position, Utf16String text_before_cursor, Utf16String text_after_cursor);
    void did_change_theme_color(Gfx::Color color);
    void did_change_background_color(Gfx::Color color);
    void did_insert_clipboard_item(Web::Clipboard::SystemClipboardItem item, String);
    void did_change_audio_play_state(Web::HTML::AudioPlayState play_state);
    void did_change_screen_wake_lock_state(Web::ScreenWakeLockState wake_lock_state);
    void did_update_session_history_entry_navigation_api_state(Web::HTML::CrossProcessId navigable_id, Web::HTML::SessionHistoryEntryIdentity entry_identity, Web::HTML::StorageSerializationRecord navigation_api_state);
    void did_update_session_history_entry_document_state_navigable_target_name(Web::HTML::CrossProcessId navigable_id, Web::HTML::SessionHistoryEntryIdentity entry_identity, Utf16String navigable_target_name);
    void did_set_session_history_entry_document_state_reload_pending(Web::HTML::CrossProcessId navigable_id, Utf16String navigation_api_key, bool reload_pending);
    void did_change_focused_navigable(Web::HTML::CrossProcessId navigable_id);
    void did_request_key_event_for_testing(Web::KeyEvent event);
    void request_history_operation(Web::HTML::CrossProcessId operation_id, Web::HistoryOperationParameters parameters);
    void history_operation_ready(Web::HTML::CrossProcessId operation_id, Web::HistoryOperationReadyResult result);
    void history_step_unload_cancelation_result(Web::HTML::CrossProcessId operation_id, Web::HTML::HistoryStepResult result, Web::HTML::UnloadPromptShown unload_prompt_shown);
    void beforeunload_check_result(Web::HTML::CrossProcessId operation_id, Web::HTML::HistoryStepResult result, Web::HTML::UnloadPromptShown unload_prompt_shown);
    void changing_navigable_history_job_ready(Web::HTML::CrossProcessId operation_id, Web::HTML::CrossProcessId navigable_id, Web::HTML::ChangingNavigableHistoryStepJobDisposition disposition, Web::HTML::UnloadDisplayedDocument unload_displayed_document);
    void changing_navigable_unload_preparation_complete(Web::HTML::CrossProcessId operation_id, Web::HTML::CrossProcessId navigable_id);
    void descendant_unload_task_complete(Web::HTML::CrossProcessId unload_id, Web::HTML::CrossProcessId navigable_id);
    void request_navigable_document_abort(Web::HTML::CrossProcessId navigable_id);
    void request_navigable_document_unfullscreen(Web::HTML::CrossProcessId navigable_id);
    void request_child_navigable_unload(Web::HTML::CrossProcessId navigable_id);
    void changing_navigable_continuation_applied(Web::HTML::CrossProcessId operation_id, Web::HTML::CrossProcessId navigable_id, Optional<Web::HTML::ReplicatedNavigableState> activated_navigable_state, Optional<Web::HTML::SessionHistoryEntryPersistedState> previous_entry_persisted_state);
    void nonchanging_navigable_history_state_updated(Web::HTML::CrossProcessId operation_id, Web::HTML::CrossProcessId navigable_id);

    void did_request_close_of_traversable(Web::HTML::CrossProcessId navigable_id, Web::HTML::CrossProcessId source_navigable_id);
    void did_inspect_dom_tree(String dom_tree);
    void did_inspect_dom_node(DOMNodeProperties properties);
    void did_finish_editing_dom_node(Optional<Web::UniqueNodeID> node_id);
    void did_mutate_dom(Mutation mutation);
    void did_get_dom_node_html(String html);
    void did_resolve_dom_node_url(u64 request_id, String resolved_url);
    void did_receive_network_response_headers(u64 request_id, u32 status_code, Optional<String> reason_phrase, Vector<HTTP::Header> response_headers, Requests::CameFromCache came_from_cache);
    void did_change_storage_item(Web::StorageAPI::StorageEndpointType storage_endpoint, String url, Optional<Utf16String> key, Optional<Utf16String> old_value, Optional<Utf16String> new_value);
    void did_update_indexed_database(String update);
    void did_request_clipboard_entries(u64 request_id);
    void did_request_set_system_focus(bool has_system_focus);
    void did_request_set_system_visibility_state(Web::HTML::VisibilityState visibility_state);

    bool operator==(WebContentPage const& other) const { return client.ptr() == other.client.ptr() && id == other.id; }
};

}
