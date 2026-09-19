/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <LibDevTools/StorageHelpers.h>
#include <LibWebView/CanonicalBrowsingContextGroup.h>
#include <LibWebView/CanonicalTraversable.h>
#include <LibWebView/StorageJar.h>
#include <LibWebView/ViewImplementation.h>
#include <LibWebView/WebContentClient.h>
#include <LibWebView/WebContentPage.h>

namespace WebView {

static JsonObject parse_json(StringView json, StringView name)
{
    auto parsed_tree = JsonValue::from_string(json);
    if (parsed_tree.is_error()) {
        dbgln("Unable to parse {}: {}", name, parsed_tree.error());
        return {};
    }

    if (!parsed_tree.value().is_object()) {
        dbgln("Expected {} to be an object: {}", name, parsed_tree.value());
        return {};
    }

    return move(parsed_tree.release_value().as_object());
}

static JsonArray parse_json_array(StringView json, StringView name)
{
    auto parsed_tree = JsonValue::from_string(json);
    if (parsed_tree.is_error()) {
        dbgln("Unable to parse {}: {}", name, parsed_tree.error());
        return {};
    }

    if (!parsed_tree.value().is_array()) {
        dbgln("Expected {} to be an array: {}", name, parsed_tree.value());
        return {};
    }

    return move(parsed_tree.release_value().as_array());
}

static Optional<JsonObject> parse_optional_json_object(StringView json, StringView name)
{
    auto parsed_tree = JsonValue::from_string(json);
    if (parsed_tree.is_error()) {
        dbgln("Unable to parse {}: {}", name, parsed_tree.error());
        return {};
    }

    if (parsed_tree.value().is_null())
        return {};

    if (!parsed_tree.value().is_object()) {
        dbgln("Expected {} to be an object or null: {}", name, parsed_tree.value());
        return {};
    }

    return move(parsed_tree.release_value().as_object());
}

static ErrorOr<Vector<DevTools::DevToolsDelegate::StorageItem>> parse_storage_items(String const& storage_items)
{
    auto parsed_items = JsonValue::from_string(storage_items);
    if (parsed_items.is_error())
        return Error::from_string_literal("Unable to parse storage items");

    if (!parsed_items.value().is_array())
        return Error::from_string_literal("Expected storage items to be an array");

    Vector<DevTools::DevToolsDelegate::StorageItem> items;
    parsed_items.value().as_array().for_each([&](auto const& item) {
        if (!item.is_object())
            return;

        auto name = item.as_object().get_string("name"sv);
        auto value = item.as_object().get_string("value"sv);
        if (!name.has_value() || !value.has_value())
            return;

        items.append({ name.release_value(), value.release_value() });
    });
    return items;
}

// The page hosting a navigable this page only represents, if it is open.
Optional<WebContentPage> WebContentPage::endpoint_hosting_navigable_represented_by(CanonicalTraversable& traversable, Web::HTML::CrossProcessId navigable_id) const
{
    auto target = traversable.find(navigable_id);
    if (!target.has_value() || traversable.hosts(*target, *this))
        return {};

    auto endpoint = traversable.page_hosting(*target);
    if (!endpoint.is_open())
        return {};
    return endpoint;
}

bool WebContentPage::is_open() const
{
    return client && client->is_page_open(id);
}

CanonicalTraversable* WebContentPage::traversable() const
{
    return client ? client->traversable_for_page(id) : nullptr;
}

Optional<ViewImplementation&> WebContentPage::view() const
{
    if (!client)
        return {};
    return client->view_displaying_page(id);
}

Optional<ViewImplementation&> WebContentPage::owning_view() const
{
    auto* traversable = this->traversable();
    if (!traversable)
        return {};
    return ViewImplementation::find_view_for_traversable(*traversable);
}

Optional<CanonicalNavigable&> WebContentPage::hosted_navigable(Web::HTML::CrossProcessId navigable_id) const
{
    if (!client)
        return {};
    return client->hosted_navigable_for_page(id, navigable_id);
}

bool WebContentPage::needs_beforeunload_check() const
{
    return client && client->page_needs_beforeunload_check(id);
}

void WebContentPage::did_request_navigation_of_navigable(Web::HTML::CrossProcessId navigable_id, Web::HTML::PreparedNavigationDescriptor navigation)
{
    // The request continues navigate at step 8 in the process hosting the target's document.
    auto* traversable = this->traversable();
    if (!traversable)
        return;

    auto endpoint = endpoint_hosting_navigable_represented_by(*traversable, navigable_id);
    if (!endpoint.has_value())
        return;
    endpoint->client->async_navigate_navigable(endpoint->id, navigable_id, move(navigation));
}

void WebContentPage::did_post_message_to_navigable(Web::HTML::CrossProcessId navigable_id, Web::HTML::PostedMessageDescriptor message)
{
    // The window post message steps queue their task on the target window in the process hosting its document.
    auto* traversable = this->traversable();
    if (!traversable)
        return;

    auto endpoint = endpoint_hosting_navigable_represented_by(*traversable, navigable_id);
    if (!endpoint.has_value())
        return;
    endpoint->client->async_deliver_posted_message(endpoint->id, navigable_id, move(message));
}

void WebContentPage::did_request_focusing_steps_for_navigable(Web::HTML::CrossProcessId navigable_id, Web::HTML::FocusTrigger focus_trigger)
{
    // The focusing steps for a navigable container go on in the process hosting its content navigable's document.
    auto* traversable = this->traversable();
    if (!traversable)
        return;

    auto endpoint = endpoint_hosting_navigable_represented_by(*traversable, navigable_id);
    if (!endpoint.has_value())
        return;
    endpoint->client->async_run_focusing_steps_for_navigable(endpoint->id, navigable_id, focus_trigger);
}

void WebContentPage::did_request_window_focus_of_navigable(Web::HTML::CrossProcessId navigable_id)
{
    // window.focus() on a window another process hosts runs there.
    auto* traversable = this->traversable();
    if (!traversable)
        return;

    auto endpoint = endpoint_hosting_navigable_represented_by(*traversable, navigable_id);
    if (!endpoint.has_value())
        return;
    endpoint->client->async_focus_window_of_navigable(endpoint->id, navigable_id);
}

void WebContentPage::did_request_set_opener_of_navigable(Web::HTML::CrossProcessId navigable_id, Web::HTML::CrossProcessId opener_navigable_id)
{
    // window.open() on a navigable another process hosts sets the opener of its active browsing context there, to that
    // of a navigable the requesting page hosts.
    auto* traversable = this->traversable();
    if (!traversable || !hosted_navigable(opener_navigable_id).has_value())
        return;

    auto endpoint = endpoint_hosting_navigable_represented_by(*traversable, navigable_id);
    if (!endpoint.has_value())
        return;
    endpoint->client->async_set_opener_of_navigable(endpoint->id, navigable_id, opener_navigable_id);
}

void WebContentPage::did_completely_finish_loading(Web::HTML::CrossProcessId navigable_id)
{
    // Only the process hosting a navigable's active document speaks for it.
    auto navigable = hosted_navigable(navigable_id);
    if (!navigable.has_value())
        return;
    navigable->active_document_completely_finished_loading();
}

void WebContentPage::did_create_child_frame(Web::HTML::CrossProcessId parent_frame_id, Web::HTML::CrossProcessId frame_id, Web::HTML::ReplicatedNavigableState replicated_state)
{
    auto* host = this->traversable();
    if (!host)
        return;
    auto& traversable = host->top_level_traversable();

    // A process materializing a frame that exists re-hosts its document. The canonical navigable's browsing context
    // stays as it is.
    if (auto existing_navigable = traversable.find(frame_id); existing_navigable.has_value()) {
        traversable.insert(*this, move(parent_frame_id), move(frame_id), move(replicated_state), existing_navigable->active_browsing_context(), *host);
        return;
    }

    // https://html.spec.whatwg.org/multipage/document-sequences.html#create-a-new-child-navigable
    // 2. Let group be element's node document's browsing context's top-level browsing context's group.
    auto group = traversable.browsing_context_for_document_creation(*this).group();
    VERIFY(group);

    // 3. Let browsingContext and document be the result of creating a new browsing context and document given element's node document, element, and group.
    auto browsing_context = CanonicalBrowsingContext::create_a_new_browsing_context_and_document(*group, replicated_state.active_document_origin, *client);

    // 6. Let documentState be a new document state, with [...]
    // 7. Let navigable be a new navigable.
    // 8. Initialize the navigable navigable given documentState and parentNavigable.
    traversable.insert(*this, move(parent_frame_id), move(frame_id), move(replicated_state), move(browsing_context), *host);
}

void WebContentPage::did_set_browser_zoom(double factor)
{
    if (auto view = owning_view(); view.has_value())
        view->set_zoom(factor);
}

void WebContentPage::did_find_in_page(size_t current_match_index, Optional<size_t> total_match_count)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_find_in_page)
            view->on_find_in_page(current_match_index, total_match_count);
    }
}

void WebContentPage::did_request_refresh()
{
    if (auto view = this->view(); view.has_value())
        view->reload();
}

void WebContentPage::did_request_cursor_change(Gfx::Cursor cursor)
{
    if (auto view = owning_view(); view.has_value())
        view->did_request_cursor_change({}, move(cursor));
}

void WebContentPage::did_update_editing_history_state(bool can_undo, bool can_redo)
{
    // Undo and redo go to the page hosting the tab's focused navigable.
    auto view = owning_view();
    if (!view.has_value())
        return;
    auto host = view->traversable().focused_navigable_host();
    if (host != *this)
        return;
    view->set_editing_history_state(can_undo, can_redo);
}

void WebContentPage::did_request_tooltip_override(Gfx::IntPoint position, ByteString title)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_request_tooltip_override)
            view->on_request_tooltip_override(view->to_widget_position(position), title);
    }
}

void WebContentPage::did_stop_tooltip_override()
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_stop_tooltip_override)
            view->on_stop_tooltip_override();
    }
}

void WebContentPage::did_enter_tooltip_area(ByteString title)
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_enter_tooltip_area)
            view->on_enter_tooltip_area(title);
    }
}

void WebContentPage::did_leave_tooltip_area()
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_leave_tooltip_area)
            view->on_leave_tooltip_area();
    }
}

void WebContentPage::did_hover_link(URL::URL url)
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_link_hover)
            view->on_link_hover(url);
    }
}

void WebContentPage::did_unhover_link()
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_link_unhover)
            view->on_link_unhover();
    }
}

void WebContentPage::did_click_link(URL::URL url, ByteString target, unsigned modifiers)
{
    auto open_in_background = modifiers == Web::UIEvents::Mod_PlatformCtrl;
    auto open_in_foreground = modifiers == (Web::UIEvents::Mod_PlatformCtrl | Web::UIEvents::Mod_Shift);
    if (open_in_background || open_in_foreground || target == "_blank"sv) {
        if (auto view = owning_view(); view.has_value())
            view->open_url_in_new_tab(url, open_in_background ? Web::HTML::ActivateTab::No : Web::HTML::ActivateTab::Yes);
    } else if (auto view = owning_view(); view.has_value()) {
        view->load(url);
    }
}

void WebContentPage::did_middle_click_link(URL::URL url, ByteString, unsigned)
{
    if (auto view = owning_view(); view.has_value())
        view->open_url_in_new_tab(url, Web::HTML::ActivateTab::No);
}

void WebContentPage::did_request_external_url(URL::URL url, URL::Origin initiator_origin, bool has_transient_activation)
{
    if (auto view = owning_view(); view.has_value())
        view->handle_external_url({}, move(url), move(initiator_origin), has_transient_activation);
}

void WebContentPage::did_inspect_storage(u64 request_id, String storage_items)
{
    if (auto view = this->view(); view.has_value()) {
        auto handler = view->on_received_storage_items.take(request_id);
        if (handler.has_value())
            (*handler)(parse_storage_items(storage_items));
    }
}

void WebContentPage::did_inspect_grid_layouts(String grid_layouts)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_received_grid_layouts)
            view->on_received_grid_layouts(parse_json_array(grid_layouts, "grid layouts"sv));
    }
}

void WebContentPage::did_inspect_current_grid(String grid_layout)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_received_current_grid)
            view->on_received_current_grid(parse_optional_json_object(grid_layout, "current grid"sv));
    }
}

void WebContentPage::did_inspect_current_flexbox(String flexbox_layout)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_received_current_flexbox)
            view->on_received_current_flexbox(parse_optional_json_object(flexbox_layout, "current flexbox"sv));
    }
}

void WebContentPage::did_inspect_indexed_database(u64 request_id, String result)
{
    if (auto view = this->view(); view.has_value())
        view->did_receive_indexed_database_inspection(request_id, parse_json(result, "IndexedDB inspection result"sv));
}

void WebContentPage::did_inspect_accessibility_tree(String accessibility_tree)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_received_accessibility_tree)
            view->on_received_accessibility_tree(parse_json(accessibility_tree, "accessibility tree"sv));
    }
}

void WebContentPage::did_get_hovered_node_id(Web::UniqueNodeID node_id)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_received_hovered_node_id)
            view->on_received_hovered_node_id(node_id);
    }
}

void WebContentPage::did_get_node_id_at_position(u64 request_id, Web::UniqueNodeID node_id)
{
    if (auto view = this->view(); view.has_value()) {
        view->did_receive_node_picker_hit_test(request_id, node_id);
    }
}

void WebContentPage::did_list_style_sheets(Vector<Web::CSS::StyleSheetIdentifier> stylesheets)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_received_style_sheet_list)
            view->on_received_style_sheet_list(stylesheets);
    }
}

void WebContentPage::did_get_style_sheet_source(Web::CSS::StyleSheetIdentifier identifier, URL::URL base_url, Utf16String source)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_received_style_sheet_source)
            view->on_received_style_sheet_source(identifier, base_url, source);
    }
}

void WebContentPage::did_list_devtools_sources(u64 request_id, Vector<Web::HTML::ScriptRegistry::Description> sources)
{
    if (auto view = this->view(); view.has_value()) {
        auto handler = view->on_received_devtools_sources.take(request_id);
        if (handler.has_value())
            (*handler)(move(sources));
    }
}

void WebContentPage::did_get_devtools_source(Web::HTML::ScriptRegistry::Identifier source_id, Optional<Web::HTML::ScriptRegistry::Content> source)
{
    if (auto view = this->view(); view.has_value()) {
        auto handler = view->on_received_devtools_source.take(source_id);
        if (handler.has_value())
            (*handler)(move(source));
    }
}

void WebContentPage::did_add_devtools_source(Web::HTML::ScriptRegistry::Description source)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_devtools_source_available)
            view->on_devtools_source_available(move(source));
    }
}

void WebContentPage::did_pause_debugger(DebuggerPause pause)
{
    if (auto view = this->view(); view.has_value()) {
        view->did_pause_debugger({});
        if (view->on_debugger_paused)
            view->on_debugger_paused(move(pause));
    }
}

void WebContentPage::did_resume_debugger()
{
    if (auto view = this->view(); view.has_value())
        view->did_resume_debugger({});
}

void WebContentPage::did_complete_debugger_breakpoint_operation(u64 request_id, Optional<String> error)
{
    if (auto view = this->view(); view.has_value())
        view->did_complete_debugger_breakpoint_operation(request_id, move(error));
}

void WebContentPage::did_take_screenshot(Gfx::ShareableBitmap screenshot)
{
    if (auto view = this->view(); view.has_value())
        view->did_receive_screenshot({}, screenshot);
}

void WebContentPage::did_get_internal_page_info(WebView::PageInfoType type, Optional<Core::AnonymousBuffer> info)
{
    if (auto view = this->view(); view.has_value())
        view->did_receive_internal_page_info({}, type, info);
}

void WebContentPage::did_get_selected_text(u64 request_id, ByteString selection)
{
    if (auto view = owning_view(); view.has_value())
        view->did_receive_selected_text({}, request_id, move(selection));
}

void WebContentPage::did_get_selected_text_for_lookup(u64 request_id, Optional<DictionaryLookup> lookup)
{
    auto view = owning_view();
    if (!view.has_value())
        return;

    // The page hosting the tab's focused navigable gives the baseline origin in the viewport of its local root.
    if (lookup.has_value() && lookup->baseline_origin.has_value())
        lookup->baseline_origin->translate_by(view->traversable().focused_navigable_host_offset().to_type<int>());
    view->did_receive_selected_text_for_lookup({}, request_id, move(lookup));
}

void WebContentPage::did_select_word_for_dictionary_lookup(u64 request_id, bool selected)
{
    if (auto view = owning_view(); view.has_value())
        view->did_select_word_for_dictionary_lookup({}, request_id, selected);
}

void WebContentPage::did_cut_selected_text(u64 request_id, ByteString selection)
{
    if (auto view = owning_view(); view.has_value())
        view->did_cut_selected_text({}, request_id, move(selection));
}

void WebContentPage::did_execute_js_console_input(JsonValue result)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_received_js_console_result)
            view->on_received_js_console_result(move(result));
    }
}

void WebContentPage::did_output_js_console_message(ConsoleOutput console_output)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_console_message)
            view->on_console_message(move(console_output));
    }
}

void WebContentPage::did_start_network_request(u64 request_id, URL::URL url, ByteString method, Vector<HTTP::Header> request_headers, ByteBuffer request_body, Optional<String> initiator_type, String referrer_policy, bool is_navigation_request, Web::Fetch::Infrastructure::Request::Priority priority)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_network_request_started)
            view->on_network_request_started(request_id, url, method, request_headers, move(request_body), move(initiator_type), move(referrer_policy), is_navigation_request, priority);
    }
}

void WebContentPage::did_receive_network_response_body(u64 request_id, ByteBuffer data)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_network_response_body_received)
            view->on_network_response_body_received(request_id, move(data));
    }
}

void WebContentPage::did_finish_network_request(u64 request_id, u64 body_size, Requests::RequestTimingInfo timing_info, Optional<Requests::NetworkError> network_error)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_network_request_finished)
            view->on_network_request_finished(request_id, body_size, timing_info, network_error);
    }
}

void WebContentPage::did_request_set_prompt_text(Utf16String message)
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_request_set_prompt_text)
            view->on_request_set_prompt_text(message);
    }
}

void WebContentPage::did_request_accept_dialog()
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_request_accept_dialog)
            view->on_request_accept_dialog();
    }
}

void WebContentPage::did_request_dismiss_dialog()
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_request_dismiss_dialog)
            view->on_request_dismiss_dialog();
    }
}

void WebContentPage::did_request_document_cookie_version_index(i64 document_id, String domain)
{
    if (auto view = this->view(); view.has_value()) {
        if (auto document_index = view->ensure_document_cookie_version_index({}, domain); !document_index.is_error())
            client->async_set_document_cookie_version_index(id, document_id, document_index.value());
    }
}

Messages::WebContentClient::DidRequestStorageItemResponse WebContentPage::did_request_storage_item(Web::StorageAPI::StorageEndpointType storage_endpoint, String storage_key, Utf16String bottle_key)
{
    auto* storage_jar = client->storage_jar_for_page(id, storage_endpoint);
    if (!storage_jar)
        return Optional<Utf16String> {};
    return storage_jar->get_item(storage_endpoint, storage_key, bottle_key);
}

Messages::WebContentClient::DidSetStorageItemResponse WebContentPage::did_set_storage_item(Web::StorageAPI::StorageEndpointType storage_endpoint, String storage_key, Utf16String bottle_key, Utf16String value)
{
    auto* storage_jar = client->storage_jar_for_page(id, storage_endpoint);
    if (!storage_jar)
        return WebView::StorageOperationError::QuotaExceededError;
    return storage_jar->set_item(storage_endpoint, storage_key, bottle_key, value);
}

void WebContentPage::did_remove_storage_item(Web::StorageAPI::StorageEndpointType storage_endpoint, String storage_key, Utf16String bottle_key)
{
    if (auto* storage_jar = client->storage_jar_for_page(id, storage_endpoint))
        storage_jar->remove_item(storage_endpoint, storage_key, bottle_key);
}

Messages::WebContentClient::DidRequestStorageKeysResponse WebContentPage::did_request_storage_keys(Web::StorageAPI::StorageEndpointType storage_endpoint, String storage_key)
{
    auto* storage_jar = client->storage_jar_for_page(id, storage_endpoint);
    if (!storage_jar)
        return Vector<Utf16String> {};
    return storage_jar->get_all_keys(storage_endpoint, storage_key);
}

void WebContentPage::did_clear_storage(Web::StorageAPI::StorageEndpointType storage_endpoint, String storage_key)
{
    if (auto* storage_jar = client->storage_jar_for_page(id, storage_endpoint))
        storage_jar->clear_storage_key(storage_endpoint, storage_key);
}

void WebContentPage::did_request_activate_tab()
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_activate_tab)
            view->on_activate_tab();
    }
}

void WebContentPage::did_change_needs_beforeunload_check(bool needs_beforeunload_check)
{
    if (auto* page = client->find_page(id))
        page->needs_beforeunload_check = needs_beforeunload_check;
}

void WebContentPage::did_consume_user_activation(Web::HTML::UserActivationConsumption consumption)
{
    auto* page_host = this->traversable();
    if (!page_host)
        return;
    page_host->top_level_traversable().for_each_hosting_page([&](WebContentPage const& page) {
        if (page == *this)
            return;
        page.client->async_consume_user_activation(page.id, consumption);
    });
}

void WebContentPage::webdriver_user_prompt_handling_complete(u64 request_id, Web::WebDriver::Response response)
{
    if (auto view = this->view(); view.has_value())
        view->did_complete_webdriver_user_prompt_handling({}, request_id, move(response));
}

void WebContentPage::webdriver_did_set_current_browsing_context(u64 command_id, Web::HTML::CrossProcessId navigable_id)
{
    if (auto view = owning_view(); view.has_value())
        view->did_set_webdriver_current_browsing_context({}, command_id, navigable_id);
}

void WebContentPage::webdriver_command_complete(u64 command_id, Web::WebDriver::Response response)
{
    if (auto view = owning_view(); view.has_value())
        view->did_complete_webdriver_content_command({}, command_id, move(response));
}

void WebContentPage::did_update_resource_count(i32 count_waiting)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_resource_status_change)
            view->on_resource_status_change(count_waiting);
    }
}

void WebContentPage::did_request_restore_window()
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_restore_window)
            view->on_restore_window();
    }
}

void WebContentPage::did_request_reposition_window(Gfx::IntPoint position, u64 completion_id)
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_reposition_window)
            view->on_reposition_window(position);
    }
    client->async_did_complete_window_rect_request(id, completion_id);
}

void WebContentPage::did_request_resize_window(Gfx::IntSize size, u64 completion_id)
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_resize_window)
            view->on_resize_window(size);
    }
    client->async_did_complete_window_rect_request(id, completion_id);
}

void WebContentPage::did_request_maximize_window(u64 completion_id)
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_maximize_window)
            view->on_maximize_window();
    }
    client->async_did_complete_window_rect_request(id, completion_id);
}

void WebContentPage::did_request_minimize_window()
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_minimize_window)
            view->on_minimize_window();
    }
}

void WebContentPage::did_request_fullscreen_window()
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_fullscreen_window)
            view->on_fullscreen_window();
    }
}

void WebContentPage::did_request_exit_fullscreen()
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_exit_fullscreen_window)
            view->on_exit_fullscreen_window();
    }
}

void WebContentPage::did_request_file(ByteString path, i32 request_id)
{
    auto file = Core::File::open(path, Core::File::OpenMode::Read);
    if (file.is_error())
        client->async_handle_file_return(id, file.error().code(), {}, request_id);
    else
        client->async_handle_file_return(id, 0, IPC::File::adopt_file(file.release_value()), request_id);
}

void WebContentPage::did_request_color_picker(Color current_color)
{
    if (auto view = owning_view(); view.has_value())
        view->did_request_color_picker({}, *this, current_color);
}

void WebContentPage::did_request_geolocation_position(u64 request_id)
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_request_geolocation_position)
            view->on_request_geolocation_position(*this, request_id);
    }
}

void WebContentPage::did_cancel_geolocation_position_request(u64 request_id)
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_cancel_geolocation_position_request)
            view->on_cancel_geolocation_position_request(*this, request_id);
    }
}

void WebContentPage::did_start_geolocation_position_watch(u64 request_id)
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_start_geolocation_position_watch)
            view->on_start_geolocation_position_watch(*this, request_id);
    }
}

void WebContentPage::did_stop_geolocation_position_watch(u64 request_id)
{
    if (auto view = owning_view(); view.has_value()) {
        if (view->on_stop_geolocation_position_watch)
            view->on_stop_geolocation_position_watch(*this, request_id);
    }
}

void WebContentPage::did_request_file_picker(Web::HTML::FileFilter accepted_file_types, Web::HTML::AllowMultipleFiles allow_multiple_files)
{
    if (auto view = owning_view(); view.has_value())
        view->did_request_file_picker({}, *this, accepted_file_types, allow_multiple_files);
}

void WebContentPage::did_finish_handling_input_event(u64 event_id, Web::EventResult event_result)
{
    if (auto view = this->view(); view.has_value()) {
        view->did_finish_handling_input_event({}, event_id, event_result);
        return;
    }

    // The view displaying the tab handed the event down; it hears the result.
    if (auto* traversable = this->traversable()) {
        auto endpoint = traversable->page_hosting(*traversable);
        if (endpoint.client && endpoint != *this)
            endpoint.client->did_finish_handling_input_event(endpoint.id, event_id, event_result);
    }
}

void WebContentPage::did_update_input_method_state(Optional<Web::DevicePixelRect> caret_rect, bool is_enabled, i32 cursor_position, i32 anchor_position, Utf16String text_before_cursor, Utf16String text_after_cursor)
{
    auto view = owning_view();
    if (!view.has_value())
        return;

    // The page hosting the tab's focused navigable describes its text input, in the viewport of its local root.
    auto& traversable = view->traversable();
    auto host = traversable.focused_navigable_host();
    if (host != *this)
        return;
    if (caret_rect.has_value())
        caret_rect->translate_by(traversable.focused_navigable_host_offset());
    view->set_input_method_state({}, { is_enabled, cursor_position, anchor_position, move(text_before_cursor), move(text_after_cursor), caret_rect });
}

void WebContentPage::did_change_theme_color(Gfx::Color color)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_theme_color_change)
            view->on_theme_color_change(color);
    }
}

void WebContentPage::did_change_background_color(Gfx::Color color)
{
    if (auto view = this->view(); view.has_value())
        view->did_change_background_color({}, color);
}

void WebContentPage::did_insert_clipboard_item(Web::Clipboard::SystemClipboardItem item, String)
{
    if (auto view = owning_view(); view.has_value())
        view->insert_clipboard_item(move(item));
}

void WebContentPage::did_change_audio_play_state(Web::HTML::AudioPlayState play_state)
{
    if (auto view = this->view(); view.has_value())
        view->did_change_audio_play_state({}, play_state);
}

void WebContentPage::did_change_screen_wake_lock_state(Web::ScreenWakeLockState wake_lock_state)
{
    if (auto view = this->view(); view.has_value())
        view->did_change_screen_wake_lock_state({}, wake_lock_state);
}

void WebContentPage::did_update_session_history_entry_navigation_api_state(Web::HTML::CrossProcessId navigable_id, Web::HTML::SessionHistoryEntryIdentity entry_identity, Web::HTML::StorageSerializationRecord navigation_api_state)
{
    auto navigable = hosted_navigable(navigable_id);
    if (!navigable.has_value())
        return;
    navigable->top_level_traversable().update_session_history_entry_navigation_api_state(*navigable, entry_identity, move(navigation_api_state));
}

void WebContentPage::did_update_session_history_entry_document_state_navigable_target_name(Web::HTML::CrossProcessId navigable_id, Web::HTML::SessionHistoryEntryIdentity entry_identity, Utf16String navigable_target_name)
{
    auto navigable = hosted_navigable(navigable_id);
    if (!navigable.has_value())
        return;
    navigable->top_level_traversable().update_session_history_entry_document_state_navigable_target_name(*navigable, entry_identity, move(navigable_target_name));
}

void WebContentPage::did_set_session_history_entry_document_state_reload_pending(Web::HTML::CrossProcessId navigable_id, Utf16String navigation_api_key, bool reload_pending)
{
    auto navigable = hosted_navigable(navigable_id);
    if (!navigable.has_value())
        return;
    navigable->top_level_traversable().set_session_history_entry_document_state_reload_pending(*navigable, navigation_api_key, reload_pending);
}

void WebContentPage::did_change_focused_navigable(Web::HTML::CrossProcessId navigable_id)
{
    // A page moves focus to a navigable it hosts.
    auto navigable = hosted_navigable(navigable_id);
    if (!navigable.has_value())
        return;
    navigable->top_level_traversable().set_focused_navigable(*navigable, *this);
}

void WebContentPage::did_request_key_event_for_testing(Web::KeyEvent event)
{
    if (auto view = owning_view(); view.has_value())
        view->enqueue_input_event(move(event));
}

void WebContentPage::request_history_operation(Web::HTML::CrossProcessId operation_id, Web::HistoryOperationParameters parameters)
{
    if (auto view = owning_view(); view.has_value())
        view->request_history_operation({}, *this, operation_id, move(parameters));
}

void WebContentPage::history_operation_ready(Web::HTML::CrossProcessId operation_id, Web::HistoryOperationReadyResult result)
{
    if (auto* traversable = this->traversable())
        traversable->did_receive_history_operation_ready(*this, operation_id, move(result));
}

void WebContentPage::history_step_unload_cancelation_result(Web::HTML::CrossProcessId operation_id, Web::HTML::HistoryStepResult result, Web::HTML::UnloadPromptShown unload_prompt_shown)
{
    if (auto* traversable = this->traversable())
        traversable->did_receive_history_step_unload_cancelation_result(*this, operation_id, result, unload_prompt_shown);
}

void WebContentPage::beforeunload_check_result(Web::HTML::CrossProcessId operation_id, Web::HTML::HistoryStepResult result, Web::HTML::UnloadPromptShown unload_prompt_shown)
{
    if (auto* traversable = this->traversable())
        traversable->did_receive_beforeunload_check_result(*this, operation_id, result, unload_prompt_shown);
}

void WebContentPage::changing_navigable_history_job_ready(Web::HTML::CrossProcessId operation_id, Web::HTML::CrossProcessId navigable_id, Web::HTML::ChangingNavigableHistoryStepJobDisposition disposition, Web::HTML::UnloadDisplayedDocument unload_displayed_document)
{
    if (auto* traversable = this->traversable())
        traversable->did_receive_changing_navigable_history_job_ready(*this, operation_id, navigable_id, disposition, unload_displayed_document);
}

void WebContentPage::changing_navigable_unload_preparation_complete(Web::HTML::CrossProcessId operation_id, Web::HTML::CrossProcessId navigable_id)
{
    if (auto* traversable = this->traversable())
        traversable->did_receive_changing_navigable_unload_preparation_complete(*this, operation_id, navigable_id);
}

void WebContentPage::descendant_unload_task_complete(Web::HTML::CrossProcessId unload_id, Web::HTML::CrossProcessId navigable_id)
{
    if (auto* traversable = this->traversable())
        traversable->did_receive_descendant_unload_task_complete(*this, unload_id, navigable_id);
}

void WebContentPage::request_navigable_document_abort(Web::HTML::CrossProcessId navigable_id)
{
    auto* page_host = this->traversable();
    if (!page_host)
        return;
    auto endpoint = endpoint_hosting_navigable_represented_by(*page_host, navigable_id);
    if (!endpoint.has_value())
        return;
    endpoint->client->async_abort_navigable_document(endpoint->id, navigable_id);
}

void WebContentPage::request_navigable_document_unfullscreen(Web::HTML::CrossProcessId navigable_id)
{
    auto* page_host = this->traversable();
    if (!page_host)
        return;
    auto endpoint = endpoint_hosting_navigable_represented_by(*page_host, navigable_id);
    if (!endpoint.has_value())
        return;
    endpoint->client->async_unfullscreen_navigable_document(endpoint->id, navigable_id);
}

void WebContentPage::request_child_navigable_unload(Web::HTML::CrossProcessId navigable_id)
{
    if (auto* traversable = this->traversable())
        traversable->did_receive_child_navigable_unload_request(*this, navigable_id);
}

void WebContentPage::changing_navigable_continuation_applied(Web::HTML::CrossProcessId operation_id, Web::HTML::CrossProcessId navigable_id, Optional<Web::HTML::ReplicatedNavigableState> activated_navigable_state, Optional<Web::HTML::SessionHistoryEntryPersistedState> previous_entry_persisted_state)
{
    if (auto* traversable = this->traversable())
        traversable->did_receive_changing_navigable_continuation_applied(*this, operation_id, navigable_id, move(activated_navigable_state), move(previous_entry_persisted_state));
}

void WebContentPage::nonchanging_navigable_history_state_updated(Web::HTML::CrossProcessId operation_id, Web::HTML::CrossProcessId navigable_id)
{
    if (auto* traversable = this->traversable())
        traversable->did_receive_nonchanging_navigable_history_state_updated(*this, operation_id, navigable_id);
}

void WebContentPage::did_request_close_of_traversable(Web::HTML::CrossProcessId navigable_id, Web::HTML::CrossProcessId source_navigable_id)
{
    // window.close() from a document another process hosts: the process hosting the traversable's document runs the
    // rest of its steps.
    auto* traversable = this->traversable();
    if (!traversable || traversable->id() != navigable_id)
        return;

    // The page closing it must host the navigable it closes from.
    if (!hosted_navigable(source_navigable_id).has_value())
        return;

    auto endpoint = endpoint_hosting_navigable_represented_by(*traversable, navigable_id);
    if (!endpoint.has_value())
        return;
    endpoint->client->async_close_traversable_from_script(endpoint->id, navigable_id, source_navigable_id);
}

void WebContentPage::did_inspect_dom_tree(String dom_tree)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_received_dom_tree)
            view->on_received_dom_tree(parse_json(dom_tree, "DOM tree"sv));
    }
}

void WebContentPage::did_inspect_dom_node(DOMNodeProperties properties)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_received_dom_node_properties)
            view->on_received_dom_node_properties(move(properties));
    }
}

void WebContentPage::did_finish_editing_dom_node(Optional<Web::UniqueNodeID> node_id)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_finished_editing_dom_node)
            view->on_finished_editing_dom_node(node_id);
    }
}

void WebContentPage::did_mutate_dom(Mutation mutation)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_dom_mutation_received)
            view->on_dom_mutation_received(move(mutation));
    }
}

void WebContentPage::did_get_dom_node_html(String html)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_received_dom_node_html)
            view->on_received_dom_node_html(move(html));
    }
}

void WebContentPage::did_resolve_dom_node_url(u64 request_id, String resolved_url)
{
    if (auto view = this->view(); view.has_value()) {
        auto handler = view->on_resolved_dom_node_url.take(request_id);
        if (handler.has_value())
            (*handler)(move(resolved_url));
    }
}

void WebContentPage::did_receive_network_response_headers(u64 request_id, u32 status_code, Optional<String> reason_phrase, Vector<HTTP::Header> response_headers, Requests::CameFromCache came_from_cache)
{
    if (auto view = this->view(); view.has_value()) {
        if (view->on_network_response_headers_received)
            view->on_network_response_headers_received(request_id, status_code, reason_phrase, response_headers, came_from_cache);
    }
}

void WebContentPage::did_change_storage_item(Web::StorageAPI::StorageEndpointType storage_endpoint, String url, Optional<Utf16String> key, Optional<Utf16String> old_value, Optional<Utf16String> new_value)
{
    if (auto view = this->view(); view.has_value()) {
        auto host = DevTools::storage_host_for_url(url);
        if (!host.has_value())
            return;

        DevTools::DevToolsDelegate::StorageChange::Type type;
        if (!key.has_value())
            type = DevTools::DevToolsDelegate::StorageChange::Type::Cleared;
        else if (!old_value.has_value())
            type = DevTools::DevToolsDelegate::StorageChange::Type::Added;
        else if (!new_value.has_value())
            type = DevTools::DevToolsDelegate::StorageChange::Type::Deleted;
        else
            type = DevTools::DevToolsDelegate::StorageChange::Type::Changed;

        view->notify_storage_changed({
            .storage_endpoint = storage_endpoint,
            .host = host.release_value(),
            .type = type,
            .key = key.has_value() ? Optional<String> { key->to_utf8() } : Optional<String> {},
        });
    }
}

void WebContentPage::did_update_indexed_database(String update)
{
    if (auto view = this->view(); view.has_value())
        view->notify_indexed_database_changed(parse_json(update, "IndexedDB update"sv));
}

void WebContentPage::did_request_clipboard_entries(u64 request_id)
{
    if (auto view = owning_view(); view.has_value()) {
        Vector<Web::Clipboard::SystemClipboardItem> items;
        if (auto item = view->clipboard_item(); !item.system_clipboard_representations.is_empty())
            items.append(move(item));

        client->async_retrieved_clipboard_entries(id, request_id, items);
    }
}

void WebContentPage::did_request_set_system_focus(bool has_system_focus)
{
    if (auto* traversable = this->traversable())
        traversable->set_has_system_focus(has_system_focus, *this);
}

void WebContentPage::did_request_set_system_visibility_state(Web::HTML::VisibilityState visibility_state)
{
    if (auto view = owning_view(); view.has_value())
        view->set_system_visibility_state(visibility_state);
}

}
