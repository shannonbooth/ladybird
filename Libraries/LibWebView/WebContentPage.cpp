/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Debug.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <LibCore/ElapsedTimer.h>
#include <LibCore/EventLoop.h>
#include <LibCore/StatsLog.h>
#include <LibHTTP/Cookie/ParsedCookie.h>
#include <LibWebCommon/Geolocation/GeolocationPositionErrorCode.h>
#include <LibWebCommon/HTML/BrowsingContext.h>
#include <LibWebCommon/Page/InputEvent.h>
#include <LibWebCommon/SecureContexts/AbstractOperations.h>
#include <LibWebCommon/WebDriver/Error.h>
#include <LibWebCommon/WebView/SiteIsolation.h>
#include <LibWebView/Application.h>
#include <LibWebView/BlobURLStore.h>
#include <LibWebView/BrowsingSession.h>
#include <LibWebView/CanonicalBrowsingContext.h>
#include <LibWebView/CanonicalBrowsingContextGroup.h>
#include <LibWebView/CanonicalDocument.h>
#include <LibWebView/CanonicalEnvironmentSettingsObject.h>
#include <LibWebView/CanonicalTraversable.h>
#include <LibWebView/CanonicalWindow.h>
#include <LibWebView/CookieJar.h>
#include <LibWebView/GamepadManager.h>
#include <LibWebView/HistoryStore.h>
#include <LibWebView/MicrophoneAuthorizer.h>
#include <LibWebView/NavigationLoader.h>
#include <LibWebView/NotificationManager.h>
#include <LibWebView/PageTranslation.h>
#include <LibWebView/PermissionManager.h>
#include <LibWebView/PictureInPictureManager.h>
#include <LibWebView/RecordingNotificationBackend.h>
#include <LibWebView/RequestServerManager.h>
#include <LibWebView/StorageAreaRegistry.h>
#include <LibWebView/StorageJar.h>
#include <LibWebView/URL.h>
#include <LibWebView/Utilities.h>
#include <LibWebView/ViewImplementation.h>
#include <LibWebView/WebContentClient.h>
#include <LibWebView/WebContentPage.h>
#include <LibWebView/WebContentTestClient.h>
#include <LibWebView/WebDriverBiDi.h>
#include <LibWebView/WebUI.h>
#include <LibWebView/WorkerProcessManager.h>

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

static Optional<LexicalPath> choose_download_destination_or_report_error(URL::URL const& url, ByteString const& suggested_filename)
{
    auto destination = Application::the().default_path_for_downloaded_file(suggested_filename);
    if (destination.is_error()) {
        if (!destination.error().is_errno() || destination.error().code() != ECANCELED)
            Application::the().display_error_dialog(ByteString::formatted("Unable to download {}: {}", url, destination.error()));
        return {};
    }

    return destination.release_value();
}

static bool is_download_in_progress(FileDownloader const& file_downloader, u64 download_id)
{
    auto download = file_downloader.download(download_id);
    return download.has_value() && download->status == FileDownloader::DownloadStatus::InProgress;
}

// An error page's title is about the failure, not the page it couldn't load, so it's never recorded in history.
static bool shows_error_page(CanonicalTraversable const& traversable)
{
    return traversable.active_document().creation_url() == URL::about_error();
}

static Optional<String> history_title(Utf16String const& title, URL::URL const& url)
{
    if (title.is_empty())
        return {};

    auto title_utf8 = title.to_utf8();
    if (title_utf8 == url.serialize() || title_utf8 == url.serialize(URL::ExcludeFragment::Yes))
        return {};

    return title_utf8;
}

WebContentPage::WebContentPage(WebContentClient& client, Web::PageId id, CanonicalTraversable& traversable)
    : m_client(client)
    , m_id(id)
    , m_traversable(traversable.make_weak_ptr<CanonicalTraversable>())
{
}

WebContentPage::~WebContentPage() = default;

WebContentServerProxy* WebContentPage::routed_proxy() const
{
    return routed_connection();
}

WebContentClient& WebContentPage::client() const
{
    VERIFY(m_client);
    return *m_client;
}

CanonicalTraversable& WebContentPage::traversable() const
{
    VERIFY(m_traversable);
    return *m_traversable;
}

ViewImplementation& WebContentPage::view() const
{
    return traversable().view();
}

bool WebContentPage::hosts_top_level_document() const
{
    return traversable().display_page() == this;
}

String WebContentPage::dump_process_tree() const
{
    StringBuilder builder;
    Vector<WebContentClient const*> processes;

    auto process_index = [&](WebContentClient const& process) -> size_t {
        for (size_t i = 0; i < processes.size(); ++i) {
            if (processes[i] == &process)
                return i;
        }
        processes.append(&process);
        return processes.size() - 1;
    };

    Function<void(CanonicalNavigable const&, size_t)> dump_frame_tree;
    dump_frame_tree = [&](CanonicalNavigable const& parent, size_t depth) {
        for (size_t i = 0; i < parent.children().size(); ++i) {
            auto const& child_frame = *parent.children()[i];

            builder.append_repeated(' ', depth * 2);
            builder.appendff("iframe#{}: {}", i, child_frame.has_remote_host() ? "remote"sv : "local"sv);
            if (child_frame.has_remote_host())
                builder.appendff(" WebContent#{}", process_index(child_frame.remote_host().client()));
            builder.append('\n');

            dump_frame_tree(child_frame, depth + 1);
        }
    };

    builder.appendff("WebContent#{}\n", process_index(client()));
    dump_frame_tree(traversable(), 1);
    return builder.to_string_without_validation();
}

Optional<CanonicalNavigable&> WebContentPage::hosted_navigable(Web::HTML::CrossProcessId navigable_id) const
{
    auto navigable = traversable().find(navigable_id);
    if (!navigable.has_value() || !traversable().hosts(*navigable, *this))
        return {};
    return *navigable;
}

// A page standing in for a lost document hosts its navigable, but not the document.
bool WebContentPage::hosts_active_document_of(CanonicalNavigable const& navigable) const
{
    return navigable.active_document().host() == this;
}

// The environments a page names are the window environments of the documents it hosts, and of those populated for it
// to host.
void WebContentPage::for_each_hosted_document(Function<IterationDecision(CanonicalDocument&)> const& callback) const
{
    traversable().for_each_in_inclusive_subtree([&](CanonicalNavigable const& navigable) {
        auto decision = IterationDecision::Continue;
        if (traversable().hosts(navigable, *this))
            decision = callback(navigable.active_document());
        navigable.for_each_populated_document_state([&](CanonicalDocumentState const& document_state) {
            auto& document = *document_state.populated_document->document;
            if (decision == IterationDecision::Continue && document.host() == this)
                decision = callback(document);
        });
        return decision;
    });
}

Optional<CanonicalDocument&> WebContentPage::document_with_hosted_environment(Web::HTML::EnvironmentId const& environment_id) const
{
    Optional<CanonicalDocument&> found_document;
    for_each_hosted_document([&](CanonicalDocument& document) {
        if (document.relevant_global_object().relevant_settings_object().id() != environment_id)
            return IterationDecision::Continue;
        found_document = document;
        return IterationDecision::Break;
    });
    return found_document;
}

bool WebContentPage::hosts_an_environment_with_storage_key(Web::StorageAPI::StorageKey const& storage_key) const
{
    bool hosts_one = false;
    for_each_hosted_document([&](CanonicalDocument& document) {
        hosts_one = obtain_a_storage_key_for_non_storage_purposes(document.relevant_global_object().relevant_settings_object()) == storage_key;
        return hosts_one ? IterationDecision::Break : IterationDecision::Continue;
    });
    return hosts_one;
}

Optional<CanonicalEnvironmentSettingsObject const&> WebContentPage::hosted_environment(Web::HTML::EnvironmentId const& environment_id) const
{
    auto document = document_with_hosted_environment(environment_id);
    if (!document.has_value())
        return {};
    return document->relevant_global_object().relevant_settings_object();
}

void WebContentPage::spoof_document_origin_for_testing(Web::HTML::EnvironmentId const& environment_id, URL::Origin origin)
{
    if (auto document = document_with_hosted_environment(environment_id); document.has_value())
        document->set_origin_for_testing(move(origin));
}

// A child frame's container speaks for it only in the page holding the container. Any other page naming it, the
// traversable included, misbehaves.
bool WebContentPage::holds_container_of(CanonicalNavigable const& child_frame, StringView message_name)
{
    if (child_frame.reporting_page() == *this)
        return true;
    client().did_misbehave(message_name, "frame whose container the page does not hold"sv);
    return false;
}

RefPtr<WebContentPage> WebContentPage::endpoint_hosting_navigable_represented_by(Web::HTML::CrossProcessId navigable_id) const
{
    auto target = traversable().find(navigable_id);
    if (!target.has_value() || traversable().hosts(*target, *this))
        return {};
    auto endpoint = traversable().page_hosting(*target);
    if (!endpoint || !endpoint->is_open())
        return {};
    return endpoint;
}

bool WebContentPage::continue_navigation_population_in_selected_process(Web::HTML::CrossProcessId navigable_id, Utf16String navigation_id)
{
    // The population steps up to the response ran in the population worker, which delivered the result here.
    auto navigable = population_worker_navigable(navigable_id);
    if (!navigable.has_value())
        return false;

    auto navigation = navigable->navigation_with_id(navigation_id);
    if (!navigation || !navigation->state.has<CanonicalNavigation::AcquiringResponseBody>())
        return false;

    navigation->loader()->obtain_the_response_policies(*navigable);
    navigation->loader()->check_the_responses_adherence_to_its_embedder_policy(*navigable);

    NonnullRefPtr history_entry = *navigation->history_entry;
    auto host_or_error = navigable->obtain_page_to_populate_document(history_entry, *navigation->loader(), *this);
    if (host_or_error.is_error()) {
        warnln("Unable to obtain a page to populate the navigation's document: {}", host_or_error.error());
        return false;
    }
    auto host = host_or_error.release_value().page;

    // Obtaining the page can replace the tab's process, whose change callbacks can end the navigation.
    navigation = navigable->navigation_with_id(navigation_id);
    if (!navigation || !navigation->state.has<CanonicalNavigation::AcquiringResponseBody>())
        return false;

    auto& acquiring = navigation->state.get<CanonicalNavigation::AcquiringResponseBody>();
    auto loader = move(acquiring.loader);
    auto weak_loader = loader->make_weak_ptr();
    navigation->state = CanonicalNavigation::Populating { move(loader) };
    navigation->page = host;

    auto finish_handoff = [host, navigable_id, navigation_id, weak_loader](bool succeeded) {
        if (!weak_loader || !host->is_open())
            return;
        auto navigable = host->traversable().top_level_traversable().find(navigable_id);
        if (!navigable.has_value())
            return;
        auto navigation = navigable->navigation_with_id(navigation_id);
        if (!navigation || !navigation->state.has<CanonicalNavigation::Populating>() || navigation->loader().ptr() != weak_loader.ptr())
            return;
        if (!succeeded) {
            navigable->did_cancel_navigation(navigation_id);
            return;
        }

        // https://html.spec.whatwg.org/multipage/browsing-the-web.html#attempt-to-populate-the-history-entry's-document
        // 5. Queue a global task on the navigation and traversal task source, given navigable's active window, to run
        //    these steps:
        // NB: The page to host the document runs the task.
        host->async_populate_navigation(weak_loader->request(), weak_loader->take_result());
    };
    // Wait for the destination RequestServer to acknowledge the import before the host adopts it.
    weak_loader->move_response_body_to(host->client().request_server_site_bindings(), move(finish_handoff));
    return true;
}

// A navigation's population steps run in the process recorded as its population worker at admission, which is
// the process with the live source document. That is not necessarily the process hosting the target's document.
Optional<CanonicalNavigable&> WebContentPage::population_worker_navigable(Web::HTML::CrossProcessId navigable_id) const
{
    if (auto navigable = hosted_navigable(navigable_id); navigable.has_value())
        return navigable;

    auto navigable = traversable().top_level_traversable().find(navigable_id);
    if (!navigable.has_value() || !navigable->uncommitted_navigation() || navigable->uncommitted_navigation()->worker() != this)
        return {};
    return *navigable;
}

StorageJar* WebContentPage::storage_jar(Web::StorageAPI::StorageEndpointType storage_endpoint) const
{
    if (storage_endpoint == Web::StorageAPI::StorageEndpointType::SessionStorage)
        return &traversable().top_level_traversable().session_storage();
    return client().session().storage_jar.ptr();
}

// The browser process obtains a key a page names again from the environment it was obtained for.
Optional<String> WebContentPage::canonical_storage_key(Web::HTML::EnvironmentId const& environment_id) const
{
    auto environment = hosted_environment(environment_id);
    if (!environment.has_value())
        return {};
    auto storage_key = obtain_a_storage_key(*environment);
    if (!storage_key.has_value())
        return {};
    return storage_key->to_string();
}

// A position a page sends is in the viewport of a local root it hosts, which the tab's view places in its own.
Optional<Gfx::IntPoint> WebContentPage::tab_position(Web::HTML::CrossProcessId local_root_id, Gfx::IntPoint position) const
{
    auto local_root = hosted_navigable(local_root_id);
    if (!local_root.has_value())
        return {};
    position.translate_by(traversable().local_root_offset(*local_root).to_type<int>());
    return position;
}

ViewImplementation* WebContentPage::expect_reply_from_ui(NonnullRefPtr<PendingReplyBase> reply)
{
    auto* view = m_is_open && m_traversable ? &m_traversable->view() : nullptr;
    if (!view || !reply->is_pending()) {
        reply->cancel();
        return nullptr;
    }
    auto* pending = reply.ptr();
    reply->when_settled([weak_page = make_weak_ptr(), pending] {
        if (weak_page)
            weak_page->m_replies_from_ui.remove_all_matching([&](auto const& reply) { return reply.ptr() == pending; });
    });
    m_replies_from_ui.append(move(reply));
    return view;
}

void WebContentPage::cancel_replies_from_ui()
{
    for (auto& reply : exchange(m_replies_from_ui, {}))
        reply->cancel();
}

void WebContentPage::cancel_replies_from_renderer()
{
    for (auto& reply : exchange(m_replies_from_renderer, {}))
        reply.value->cancel();
}

u64 WebContentPage::expect_reply_from_renderer(NonnullRefPtr<PendingReplyBase> reply)
{
    if (!m_is_open) {
        reply->cancel();
        return 0;
    }
    auto request_id = m_next_renderer_request_id++;
    m_replies_from_renderer.set(request_id, move(reply));
    return request_id;
}

// A dialog blocks the whole tab, so every other page of the tab is told of the one a document of this page opened.
void WebContentPage::did_open_dialog(Web::PendingDialog dialog, Utf16String const& message)
{
    traversable().for_each_hosting_page([&](WebContentPage& page) {
        if (&page != this)
            page.async_did_open_dialog_in_another_process(dialog, message);
    });
}

static void answer_dialog(WebContentPage& page, Web::PendingDialog dialog, bool accepted, Optional<Utf16String> text)
{
    switch (dialog) {
    case Web::PendingDialog::None:
        VERIFY_NOT_REACHED();
    case Web::PendingDialog::Alert:
        page.async_alert_closed();
        return;
    case Web::PendingDialog::BeforeUnload:
        page.routed_proxy()->async_before_unload_closed(page.id(), accepted);
        return;
    case Web::PendingDialog::Confirm:
        page.async_confirm_closed(accepted);
        return;
    case Web::PendingDialog::Prompt:
        page.async_prompt_closed(accepted ? move(text) : Optional<Utf16String> {});
        return;
    }
}

// The tab shows one dialog at a time. The other pages of the tab hold it too, but only this page is owed the answer.
RefPtr<DialogReply> WebContentPage::open_dialog(Web::PendingDialog dialog, Utf16String const& message)
{
    if (view().has_open_dialog()) {
        answer_dialog(*this, dialog, dialog == Web::PendingDialog::BeforeUnload, {});
        return nullptr;
    }
    did_open_dialog(dialog, message);
    auto reply = DialogReply::create([page = NonnullRefPtr { *this }, dialog](bool accepted, Optional<Utf16String> text) {
        if (!page->m_traversable)
            return;
        page->traversable().for_each_hosting_page([&](WebContentPage& other_page) {
            if (&other_page != page.ptr())
                other_page.async_did_open_dialog_in_another_process(Web::PendingDialog::None, Utf16View {});
        });
        if (page->is_open())
            answer_dialog(*page, dialog, accepted, move(text));
    },
        dialog == Web::PendingDialog::BeforeUnload, {});
    if (!expect_reply_from_ui(reply))
        return nullptr;
    return reply;
}

void WebContentPage::maybe_record_history_visit_for_current_load(URL::URL const& url, Optional<String> title, StringView reason)
{
    auto normalized_url = HistoryStore::normalize_url(url);
    if (!normalized_url.has_value())
        return;

    if (m_history_recorded_url_for_current_load == *normalized_url) {
        dbgln_if(WEBVIEW_HISTORY_DEBUG, "[History] Visit for page {} at '{}' was already recorded during this load before {}", m_id, *normalized_url, reason);
        return;
    }

    dbgln_if(WEBVIEW_HISTORY_DEBUG, "[History] Recording history visit for page {} at '{}' after {}", m_id, *normalized_url, reason);

    // Title and favicon updates already give us a useful history entry, so
    // do not wait for did_finish_loading() on pages that never reach it.
    auto transition = view().history_visit_transition_for_current_load();
    client().session().history_store->record_visit(url, move(title), UnixDateTime::now(), transition);
    m_history_recorded_url_for_current_load = normalized_url.release_value();
}

void WebContentPage::begin_top_level_load(URL::URL const& url)
{
    if (auto process = WebView::Application::process_manager().find_child(client().child_id()); process.has_value())
        process->set_title(OptionalNone {});

    m_history_recorded_url_for_current_load.clear();

    traversable().presentation().reset_blocked_resources();
    traversable().presentation().set_loading(true);
    view().did_begin_top_level_load(url);
}

void WebContentPage::request_close()
{
    // The frontend may destroy the view immediately after this for pages that cannot prompt during beforeunload.
    // Keep owning the WebContent close until the page reports that its top-level traversable has been closed.
    m_detached_close_pending = true;
    async_request_close();
}

void WebContentPage::discard()
{
    async_discard_embedded_page();
    // The page stops being a history job endpoint now; queued history work must not start against it. Its
    // client outlives the discard acknowledgement, so a shared process is not closed under the page.
    m_detached_close_pending = true;
    client().unregister_embedded_page(m_id);
}

Web::CompositorContextId WebContentPage::compositor_context_id()
{
    if (!m_is_open)
        return Web::compositor_context_id_for_page(m_id);
    return client().compositor_context_id_for_page(*this);
}

bool WebContentPage::handle_and_dispatch_key_event_in_compositor(Web::KeyEvent const& event, bool allow_keyboard_scroll)
{
    auto posted = Application::the().handle_and_dispatch_key_event_in_compositor(compositor_context_id(), event, allow_keyboard_scroll);
    dbgln_if(COMPOSITOR_DEBUG, "[Compositor] UI posted key event {} for page {} to the compositor: {}", event.id, m_id, posted);
    return posted;
}

bool WebContentPage::handle_and_dispatch_pinch_event_in_compositor(Web::PinchEvent const& event)
{
    auto posted = Application::the().handle_and_dispatch_pinch_event_in_compositor(compositor_context_id(), event);
    dbgln_if(COMPOSITOR_DEBUG, "[Compositor] UI posted pinch event {} for page {} to the compositor: {}", event.id, m_id, posted);
    return posted;
}

bool WebContentPage::handle_and_dispatch_mouse_event_in_compositor(Web::MouseEvent const& event)
{
    auto posted = Application::the().handle_and_dispatch_mouse_event_in_compositor(compositor_context_id(), event);
    dbgln_if(COMPOSITOR_DEBUG, "[Compositor] UI posted mouse event {} (type {}) for page {} to the compositor: {}",
        event.id, to_underlying(event.type), m_id, posted);
    return posted;
}

void WebContentPage::did_consume_input_event_in_compositor(u64 event_id)
{
    dbgln_if(COMPOSITOR_DEBUG, "[Compositor] UI compositor consumed input event {} for page {}", event_id, m_id);
    if (hosts_top_level_document()) {
        view().did_consume_input_event_in_compositor(event_id);
        return;
    }

    // The view displaying the tab handed the event down; it hears the result.
    if (auto display_page = traversable().display_page(); display_page) {
        if (display_page->is_open() && display_page.ptr() != this)
            display_page->did_consume_input_event_in_compositor(event_id);
    }
}

void WebContentPage::did_not_dispatch_input_event_through_compositor(u64 event_id)
{
    dbgln_if(COMPOSITOR_DEBUG, "[Compositor] UI compositor did not dispatch input event {} for page {}; sending it to WebContent directly", event_id, m_id);
    if (hosts_top_level_document()) {
        view().did_not_dispatch_input_event_through_compositor(event_id);
        return;
    }

    if (auto display_page = traversable().display_page(); display_page) {
        if (display_page->is_open() && display_page.ptr() != this)
            display_page->did_not_dispatch_input_event_through_compositor(event_id);
    }
}

void WebContentPage::did_dispatch_input_event_through_compositor(u64 event_id, bool performed_default_action)
{
    if (hosts_top_level_document()) {
        view().did_dispatch_input_event_through_compositor(event_id, performed_default_action);
        return;
    }

    if (auto display_page = traversable().display_page(); display_page) {
        if (display_page->is_open() && display_page.ptr() != this)
            display_page->did_dispatch_input_event_through_compositor(event_id, performed_default_action);
    }
}

void WebContentPage::did_present_bitmap(Gfx::IntRect content_rect, Gfx::IntRect damage_rect, i32 bitmap_id, double device_pixel_ratio)
{
    dbgln_if(COMPOSITOR_DEBUG, "[Compositor] UI compositor IPC did_paint for page {} bitmap {} rect={}x{} at {},{}",
        m_id, bitmap_id, content_rect.width(), content_rect.height(), content_rect.x(), content_rect.y());
    if (hosts_top_level_document()) {
        view().server_did_paint(bitmap_id, content_rect.size(), damage_rect, device_pixel_ratio);
        return;
    }
    dbgln_if(COMPOSITOR_DEBUG, "[Compositor] UI dropping did_paint for page {} bitmap {}: no view", m_id, bitmap_id);
    release_presented_bitmap(bitmap_id);
}

void WebContentPage::did_present_backing_stores(Vector<i32> bitmap_ids, Vector<Gfx::SharedImage> backing_stores)
{
    dbgln_if(COMPOSITOR_DEBUG, "[Compositor] UI received {} backing stores for page {}", backing_stores.size(), m_id);
    if (!hosts_top_level_document()) {
        // The page can begin to display the tab, when the view installs them. The compositor reserves the first
        // published buffer for the UI to install as its front buffer.
        dbgln_if(COMPOSITOR_DEBUG, "[Compositor] UI keeping {} backing stores for page {} until it displays the tab", backing_stores.size(), m_id);
        if (m_presented_backing_stores.has_value() && !m_presented_backing_stores->bitmap_ids.is_empty())
            release_presented_bitmap(m_presented_backing_stores->bitmap_ids[0]);
        m_presented_backing_stores = PresentedBackingStores { move(bitmap_ids), move(backing_stores) };
        return;
    }
    view().did_allocate_backing_stores(move(bitmap_ids), move(backing_stores));
}

void WebContentPage::did_add_backing_stores(Vector<i32> bitmap_ids, Vector<Gfx::SharedImage> backing_stores)
{
    dbgln_if(COMPOSITOR_DEBUG, "[Compositor] UI received {} additional backing stores for page {}", backing_stores.size(), m_id);
    if (hosts_top_level_document()) {
        view().did_add_backing_stores(move(bitmap_ids), move(backing_stores));
        return;
    }
    if (!m_presented_backing_stores.has_value())
        return;
    m_presented_backing_stores->bitmap_ids.extend(move(bitmap_ids));
    m_presented_backing_stores->backing_stores.extend(move(backing_stores));
}

void WebContentPage::did_retire_backing_stores(ReadonlySpan<i32> bitmap_ids)
{
    dbgln_if(COMPOSITOR_DEBUG, "[Compositor] UI retiring backing stores {} for page {}", bitmap_ids, m_id);
    if (hosts_top_level_document()) {
        view().did_retire_backing_stores(bitmap_ids);
        return;
    }
    if (!m_presented_backing_stores.has_value())
        return;
    // The first store is the one the view installs as its front buffer, which the compositor never retires.
    for (size_t i = m_presented_backing_stores->bitmap_ids.size(); i-- > 1;) {
        if (!bitmap_ids.contains_slow(m_presented_backing_stores->bitmap_ids[i]))
            continue;
        m_presented_backing_stores->bitmap_ids.remove(i);
        m_presented_backing_stores->backing_stores.remove(i);
    }
}

Optional<WebContentPage::PresentedBackingStores> WebContentPage::take_presented_backing_stores()
{
    auto backing_stores = move(m_presented_backing_stores);
    m_presented_backing_stores.clear();
    return backing_stores;
}

void WebContentPage::release_presented_bitmap(i32 bitmap_id)
{
    auto context_id = Web::compositor_context_id_for_page(m_id);
    if (!m_is_open || client().page_id_for_compositor_context_id(context_id) != m_id)
        return;

    Application::the().notify_compositor_presented_bitmap_ready_to_paint(context_id, bitmap_id);
}

void WebContentPage::fail_renderer_owned_downloads()
{
    auto& file_downloader = Application::the().file_downloader();
    for (auto download_id : exchange(m_renderer_owned_downloads, {}))
        file_downloader.fail_download(download_id, "Download process exited"_string);
}

void WebContentPage::close()
{
    PictureInPictureManager::the().close_window(*this);
    NotificationManager::the().page_did_close(*this);
    m_is_open = false;
    cancel_replies_from_ui();
    cancel_replies_from_renderer();
    // NB: Nothing finishes the input this page held, including a page discarded while its process hosts other pages.
    if (m_traversable)
        m_traversable->view().did_lose_input_event_endpoint(*this);
    m_needs_beforeunload_check = true;
    m_history_recorded_url_for_current_load.clear();
    if (m_client) {
        m_client->update_audio_capture_permission();
        if (auto* bidi = WebDriverBiDi::the())
            bidi->push_session_state_for_client(*m_client);
        m_client->forget_audio_playback(m_id);
        GamepadManager::the().page_closed(*this);
    }
    if (m_traversable)
        m_traversable->presentation().page_closed(m_id);
    set_audibility(Audio::Audibility::Inaudible);
    m_web_ui = nullptr;
    auto owed_replies = move(m_owed_replies);
    for (auto owed : owed_replies)
        owe_reply(owed);
}

void WebContentPage::set_web_ui(RefPtr<WebUI> web_ui)
{
    m_web_ui = move(web_ui);
}

// A WebUI whose connection closed is dropped, unless the page has another by then.
void WebContentPage::web_ui_disconnected(Badge<WebUI>, WebUI& web_ui)
{
    if (m_web_ui == &web_ui)
        m_web_ui = nullptr;
}

void WebContentPage::owe_reply(OwedReply owed)
{
    if (m_is_open) {
        m_owed_replies.set(owed);
        return;
    }
    Core::deferred_invoke([traversable = m_traversable, owed] {
        if (traversable)
            traversable->did_not_receive_reply(owed);
    });
}

bool WebContentPage::take_owed_reply(OwedReply owed)
{
    return m_owed_replies.remove(owed);
}

static bool may_navigate_to_a_local_file(WebContentClient& requesting_client);

void WebContentPage::did_request_navigation_of_navigable(Web::HTML::CrossProcessId navigable_id, Web::HTML::CrossProcessId source_navigable_id, Web::HTML::CrossProcessId source_document_id, Web::HTML::PreparedNavigationDescriptor navigation)
{
    if (navigation.url.scheme() == "file"sv && !may_navigate_to_a_local_file(client()))
        return;

    auto target = traversable().find(navigable_id);
    if (!target.has_value())
        return;

    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#navigate
    // NB: The page ran the steps up to 7 for a sourceDocument it hosts. The UI process runs those guarding navigable
    //     again, with the sourceDocument and sandboxing flags it holds. sourceDocument can be in another tab, in this
    //     process's page for it.
    Optional<CanonicalNavigable&> source;
    RefPtr<WebContentPage> source_page;
    client().for_each_page([&](WebContentPage& page) {
        auto navigable = page.traversable().find(source_navigable_id);
        if (!navigable.has_value() || navigable->active_document().id() != source_document_id || !page.hosts_active_document_of(*navigable))
            return IterationDecision::Continue;
        source = *navigable;
        source_page = page;
        return IterationDecision::Break;
    });
    if (!source.has_value())
        return;
    auto& source_document = source->active_document();
    auto const& fetch_client = navigation.source_snapshot_params.fetch_client;
    if (!fetch_client.has_value() || fetch_client->id != source_document.relevant_global_object().relevant_settings_object().id()) {
        client().did_misbehave("did_request_navigation_of_navigable"sv, "fetch client is not the source document's environment"sv);
        return;
    }

    // 6. Otherwise:
    //    2. If sourceDocument's node navigable is not allowed by sandboxing to navigate navigable given
    //       sourceSnapshotParams:
    //       2. Return.
    Web::InitiatorSourceSnapshot source_snapshot_params {
        .sandboxing_flags = navigation.source_snapshot_params.sandboxing_flags | source_document.active_sandboxing_flag_set(),
        .has_transient_activation = navigation.source_snapshot_params.has_transient_activation,
    };
    if (!source->allowed_by_sandboxing_to_navigate(*target, source_snapshot_params))
        return;

    //    3. Set initiatorOriginSnapshot to sourceDocument's origin.
    navigation.initiator_origin_snapshot = source_document.origin();

    target->begin_navigation(move(navigation), move(source_page));
}

static bool navigation_can_violate(Web::ContentSecurityPolicy::RemoteViolationDirective directive, CanonicalNavigable const& navigable, Web::HTML::NavigationPopulationRequest const& request)
{
    switch (directive) {
    case Web::ContentSecurityPolicy::RemoteViolationDirective::FrameSrc:
    case Web::ContentSecurityPolicy::RemoteViolationDirective::ObjectSrc:
        return !navigable.is_top_level_traversable();
    case Web::ContentSecurityPolicy::RemoteViolationDirective::FormAction:
        return request.csp_navigation_type == Web::ContentSecurityPolicy::Directives::NavigationType::FormSubmission;
    }
    VERIFY_NOT_REACHED();
}

// The process populating a navigation reports a violation of a policy of its source, which goes to the page found
// hosting the source when the navigation was requested, while that page still hosts it.
void WebContentPage::did_report_navigation_violation(Web::HTML::CrossProcessId navigable_id, Utf16String navigation_id, u64 policy_index, Web::ContentSecurityPolicy::RemoteViolationDirective directive, bool url_is_https)
{
    auto navigable = population_worker_navigable(navigable_id);
    if (!navigable.has_value())
        return;
    auto navigation = navigable->navigation_with_id(navigation_id);
    auto* creating = navigation ? navigation->state.get_pointer<CanonicalNavigation::CreatingNavigationParams>() : nullptr;
    if (!creating || navigation->worker() != this)
        return;

    auto const& request = creating->loader->request();
    auto const& fetch_client = request.source_snapshot_params.fetch_client;
    if (!fetch_client.has_value() || !navigation_can_violate(directive, *navigable, request)) {
        client().did_misbehave("did_report_navigation_violation"sv, "violation the navigation cannot have"sv);
        return;
    }

    auto const& source_page = navigation->source_page;
    if (!source_page || !source_page->is_open() || !source_page->hosted_environment(fetch_client->id).has_value())
        return;

    // The request's URL can be upgraded from http to https before the check, by upgrade-insecure-requests or HSTS, so
    // the report then names the upgraded URL.
    auto url = request.history_entry.url;
    if (url_is_https && url.scheme() == "http"sv) {
        url.set_scheme("https"_string);
        // NB: Unlike the URL parser, setting the scheme does not remove a port that is the new scheme's default port.
        if (url.port() == 443)
            url.set_port({});
    }
    source_page->async_report_violation_of_environment(fetch_client->id, policy_index, directive, move(url));
}

void WebContentPage::did_post_message_to_navigable(Web::HTML::CrossProcessId navigable_id, Web::HTML::PostedMessageDescriptor posted_message)
{
    // https://html.spec.whatwg.org/multipage/web-messaging.html#window-post-message-steps
    // 2. Let incumbentSettings be the incumbent settings object.
    // NB: The posting process determined it. Any page of that process can host it, since the request comes through the
    //     page holding the target's tab.
    auto incumbent_settings = client().hosted_environment(posted_message.incumbent_settings_id);

    // 8. Queue a global task on the posted message task source given targetWindow to run the following steps:
    // NB: The process hosting targetWindow's document queues it, with the origin and source given here.
    auto endpoint = endpoint_hosting_navigable_represented_by(navigable_id);
    if (!endpoint)
        return;

    // 8.2. Let origin be incumbentSettings's origin.
    // AD-HOC: For a window whose document this process no longer holds, which a same-origin document can still call
    //         into, the origin the posting process gives is taken if that process hosts a document of that origin.
    Optional<URL::Origin> origin;
    if (incumbent_settings.has_value())
        origin = incumbent_settings->origin();
    else if (client().hosts_a_document_of_origin(posted_message.source_origin))
        origin = move(posted_message.source_origin);
    if (!origin.has_value())
        return;

    // 8.3. Let source be the WindowProxy object corresponding to incumbentSettings's global object (a Window object).
    // NB: The source must be a navigable the posting process hosts.
    Optional<Web::HTML::CrossProcessId> source_navigable_id;
    if (posted_message.source_navigable_id.has_value() && client().hosted_navigable(*posted_message.source_navigable_id).has_value())
        source_navigable_id = posted_message.source_navigable_id;

    Web::HTML::MessageDescriptor message {
        .serialize_with_transfer_result = move(posted_message.serialize_with_transfer_result),
        .target_origin = move(posted_message.target_origin),
        .source_origin = origin.release_value(),
        .source_navigable_id = source_navigable_id,
    };
    endpoint->async_deliver_posted_message(navigable_id, move(message));
}

void WebContentPage::did_request_focusing_steps_for_navigable(Web::HTML::CrossProcessId navigable_id, Web::HTML::FocusTrigger focus_trigger)
{
    auto navigable = traversable().find(navigable_id);
    if (!navigable.has_value() || !holds_container_of(*navigable, "did_request_focusing_steps_for_navigable"sv))
        return;

    // The focusing steps for a navigable container go on in the process hosting its content navigable's document.
    auto endpoint = endpoint_hosting_navigable_represented_by(navigable_id);
    if (!endpoint)
        return;
    // The requesting page has moved focus to the container, so the tab's focused navigable is the content navigable
    // from here on. Recording it before the host runs the steps keeps a report the requesting page makes meanwhile
    // ahead of the host's, which only confirms this.
    traversable().set_focused_navigable(*navigable, *this);
    endpoint->async_run_focusing_steps_for_navigable(navigable_id, focus_trigger);
}

void WebContentPage::did_request_window_focus_of_navigable(Web::HTML::CrossProcessId navigable_id)
{
    // window.focus() on a window another process hosts runs there.
    auto endpoint = endpoint_hosting_navigable_represented_by(navigable_id);
    if (!endpoint)
        return;
    endpoint->async_focus_window_of_navigable(navigable_id);
}

// https://html.spec.whatwg.org/multipage/nav-history-apis.html#window-open-steps
// 16.2. If noopener is false, then set targetNavigable's active browsing context's opener browsing context to
//       sourceDocument's browsing context.
// NB: sourceDocument's node navigable must be one the requesting process hosts. The request comes through the page
//     holding the target's tab, which is not the opener's when the two are different tabs.
void WebContentPage::did_request_set_opener_of_navigable(Web::HTML::CrossProcessId navigable_id, Web::HTML::CrossProcessId opener_navigable_id)
{
    if (!client().hosted_navigable(opener_navigable_id).has_value())
        return;
    auto* traversable = CanonicalTraversable::traversable_containing(navigable_id);
    if (!traversable)
        return;
    if (auto navigable = traversable->find(navigable_id); navigable.has_value())
        navigable->set_opener_browsing_context(opener_navigable_id);
}

void WebContentPage::did_completely_finish_loading(Web::HTML::CrossProcessId navigable_id, Web::HTML::CrossProcessId document_id)
{
    // Only the process hosting a document speaks for it.
    auto navigable = traversable().find(navigable_id);
    if (!navigable.has_value())
        return;
    auto document = navigable->document_with_id(document_id);
    if (!document || document->host() != this)
        return;
    navigable->document_completely_finished_loading(*document);
}

void WebContentPage::did_create_populated_document_with_an_origin_of_its_own(Web::HTML::CrossProcessId navigable_id, Web::HTML::CrossProcessId document_id, Web::HTML::PopulatedDocumentOrigin origin, Web::HTML::EnvironmentId environment_id)
{
    if (auto navigable = traversable().find(navigable_id); navigable.has_value())
        navigable->did_create_populated_document_with_an_origin_of_its_own(*this, document_id, origin, environment_id);
}

void WebContentPage::did_create_child_frame(Web::HTML::CrossProcessId parent_frame_id, Web::HTML::CrossProcessId container_document_id, Web::HTML::CrossProcessId frame_id, Web::HTML::HostedNavigableState hosted_state, Web::HTML::PendingSessionHistoryEntryDescriptor initial_history_entry, URL::Origin origin, Web::HTML::EnvironmentId environment_id, Web::HTML::CrossProcessId document_id)
{
    auto& traversable = this->traversable();

    // The parent can be gone by the time its frame is reported: its own container was removed meanwhile.
    auto parent_navigable = traversable.find(parent_frame_id);
    if (!parent_navigable.has_value())
        return;

    auto container_document = parent_navigable->document_with_id(container_document_id);
    if (!container_document || container_document->host() != this) {
        client().did_misbehave("did_create_child_frame"sv, "frame created in a document the page does not hold"sv);
        return;
    }

    if (traversable.find(frame_id).has_value()) {
        client().did_misbehave("did_create_child_frame"sv, "frame created with an existing navigable's id"sv);
        return;
    }

    hosted_state.container.document_active_sandboxing_flag_set = container_document->active_sandboxing_flag_set();
    hosted_state.container.document_embedder_policy = container_document->embedder_policy();
    hosted_state.container.document_permissions_policy = container_document->permissions_policy();
    traversable.adopt_nested_history_for_created_child(*parent_navigable, *container_document, frame_id);

    // https://html.spec.whatwg.org/multipage/document-sequences.html#create-a-new-child-navigable
    // NB: The process creating the navigable reports the entry that initializing it, in step 8, created.
    auto initial_entry = CanonicalSessionHistoryEntry::create_from_descriptor(Web::HTML::create_session_history_entry_descriptor(move(initial_history_entry), 0));
    if (initial_entry.is_error()) {
        client().did_misbehave("did_create_child_frame"sv, "invalid initial session history entry"sv);
        return;
    }

    // 2. Let group be element's node document's browsing context's top-level browsing context's group.
    auto group = container_document->browsing_context().top_level_browsing_context().group();

    // 3. Let browsingContext and document be the result of creating a new browsing context and document given element's node document, element, and group.
    auto document = CanonicalBrowsingContext::create_a_new_browsing_context_and_document(container_document.ptr(), hosted_state.container, *group, document_id, move(environment_id), move(origin)).document;

    // 6. Let documentState be a new document state, with
    //    document: document
    //    initiator origin: document's origin
    //    origin: document's origin
    //    navigable target name: targetName
    //    about base URL: document's about base URL
    // NB: The process creating the navigable holds documentState's other fields, and reports its ID with the navigable's
    //     state.
    auto& document_state = *initial_entry.value()->document_state;
    document_state.initiator_origin = document->origin();
    document_state.origin = document->origin();

    // 7. Let navigable be a new navigable.
    // 8. Initialize the navigable navigable given documentState and parentNavigable.
    traversable.insert(*this, *parent_navigable, *container_document, frame_id, move(hosted_state), initial_entry.release_value(), move(document));
}

void WebContentPage::did_set_browser_zoom(double factor)
{
    view().set_zoom(factor);
}

void WebContentPage::did_set_device_pixel_ratio_for_testing(double device_pixel_ratio)
{
    view().set_device_pixel_ratio_for_testing(*this, device_pixel_ratio);
}

void WebContentPage::did_find_in_page(size_t current_match_index, Optional<size_t> total_match_count)
{
    if (!hosts_top_level_document())
        return;
    if (view().on_find_in_page)
        view().on_find_in_page(current_match_index, total_match_count);
}

void WebContentPage::did_request_cursor_change(Gfx::Cursor cursor)
{
    view().did_request_cursor_change(move(cursor));
}

void WebContentPage::did_update_editing_history_state(bool can_undo, bool can_redo)
{
    // Undo and redo go to the page hosting the tab's focused navigable.
    auto host = traversable().focused_navigable_host();
    if (host != *this)
        return;
    view().set_editing_history_state(can_undo, can_redo);
}

void WebContentPage::did_set_text_fragment_indication_visibility(bool visible)
{
    traversable().presentation().did_change_text_fragment_indication_visibility(*this, visible);
}

void WebContentPage::did_enter_tooltip_area(ByteString title)
{
    view().did_enter_tooltip_area(move(title));
}

void WebContentPage::did_leave_tooltip_area()
{
    view().did_leave_tooltip_area();
}

void WebContentPage::did_hover_link(URL::URL url)
{
    if (view().on_link_hover)
        view().on_link_hover(url);
}

void WebContentPage::did_unhover_link()
{
    if (view().on_link_unhover)
        view().on_link_unhover();
}

static Optional<URL::Origin> initiator_origin_snapshot(URL::Origin const& given_origin, Web::HTML::NavigationSourceSnapshot const& source_snapshot_params, WebContentClient& hosting_client);

// A page reaches local file content only by navigating there from local file content, or from the browser's UI: a
// process holding none could otherwise come to host a file: document, and with it read every file the user can.
static bool may_navigate_to_a_local_file(WebContentClient& requesting_client)
{
    return requesting_client.may_read_local_files();
}

// A navigation a page asked the browser's UI to start for it, with the initiator origin the UI process takes from the
// fetch client — as it does for a navigation the page starts itself, so a process names only a source it hosts.
static Optional<Web::HTML::PreparedNavigationDescriptor> navigation_from_page(WebContentClient& requesting_client, Web::HTML::PreparedNavigationDescriptor navigation)
{
    // A page's own click or context menu names a document the page's process hosts, so only that process vouches for it.
    auto initiator_origin = initiator_origin_snapshot(navigation.initiator_origin_snapshot, navigation.source_snapshot_params, requesting_client);
    if (!initiator_origin.has_value())
        return {};
    if (navigation.url.scheme() == "file"sv && !may_navigate_to_a_local_file(requesting_client))
        return {};
    navigation.initiator_origin_snapshot = initiator_origin.release_value();
    return navigation;
}

void WebContentPage::did_click_link(Web::HTML::PreparedNavigationDescriptor navigation, ByteString target, unsigned modifiers)
{
    auto verified_navigation = navigation_from_page(client(), move(navigation));
    if (!verified_navigation.has_value())
        return;
    // NB: A link the browser does not handle itself is an external application's. The page asking to open it is not
    //     the browser's UI, so the request takes the page's path, which honors only input the UI process saw.
    if (!is_url_handled_internally(verified_navigation->url)) {
        view().handle_external_url(verified_navigation->url, verified_navigation->initiator_origin_snapshot, verified_navigation->source_snapshot_params.has_transient_activation && has_transient_user_activation());
        return;
    }
    auto open_in_background = modifiers == Web::UIEvents::Mod_PlatformCtrl;
    auto open_in_foreground = modifiers == (Web::UIEvents::Mod_PlatformCtrl | Web::UIEvents::Mod_Shift);
    if (open_in_background || open_in_foreground || target == "_blank"sv) {
        // NB: The renderer sends a link to open in a new tab only for the user's own click, which the view delivered.
        //     That click opens the link whatever activation the page used up for other things, such as a popup its
        //     click handler opened.
        if (!view().take_input_to_open_link_in_new_tab())
            return;
        view().open_navigation_in_new_tab(verified_navigation.release_value(), open_in_background ? Web::HTML::ActivateTab::No : Web::HTML::ActivateTab::Yes);
    } else {
        view().load(verified_navigation.release_value());
    }
}

void WebContentPage::did_middle_click_link(Web::HTML::PreparedNavigationDescriptor navigation, ByteString, unsigned)
{
    auto verified_navigation = navigation_from_page(client(), move(navigation));
    if (!verified_navigation.has_value())
        return;
    // NB: A link the browser does not handle itself is an external application's. The page asking to open it is not
    //     the browser's UI, so the request takes the page's path, which honors only input the UI process saw.
    if (!is_url_handled_internally(verified_navigation->url)) {
        view().handle_external_url(verified_navigation->url, verified_navigation->initiator_origin_snapshot, verified_navigation->source_snapshot_params.has_transient_activation && has_transient_user_activation());
        return;
    }
    // NB: The renderer sends a link to open in a new tab only for the user's own click, which the view delivered.
    //     That click opens the link whatever activation the page used up for other things.
    if (!view().take_input_to_open_link_in_new_tab())
        return;
    view().open_navigation_in_new_tab(verified_navigation.release_value(), Web::HTML::ActivateTab::No);
}

void WebContentPage::did_request_external_url(URL::URL url, URL::Origin initiator_origin, bool has_transient_activation)
{
    // NB: The page's own claim of activation counts only along with input the UI process delivered to it.
    view().handle_external_url(move(url), move(initiator_origin), has_transient_activation && has_transient_user_activation());
}

void WebContentPage::did_inspect_storage(u64 request_id, String storage_items)
{
    if (auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnStorageItemsReceived>>(request_id))
        reply->complete(parse_storage_items(storage_items));
}

void WebContentPage::did_inspect_grid_layouts(u64 request_id, String grid_layouts)
{
    if (auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnGridLayoutsReceived>>(request_id))
        reply->complete(parse_json_array(grid_layouts, "grid layouts"sv));
}

void WebContentPage::did_inspect_current_grid(u64 request_id, String grid_layout)
{
    if (auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnCurrentGridReceived>>(request_id))
        reply->complete(parse_optional_json_object(grid_layout, "current grid"sv));
}

void WebContentPage::did_inspect_current_flexbox(u64 request_id, String flexbox_layout)
{
    if (auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnCurrentFlexboxReceived>>(request_id))
        reply->complete(parse_optional_json_object(flexbox_layout, "current flexbox"sv));
}

void WebContentPage::did_inspect_indexed_database(u64 request_id, String result)
{
    auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnIndexedDBInspectionComplete>>(request_id);
    if (!reply)
        return;
    auto inspection = parse_json(result, "IndexedDB inspection result"sv);
    if (inspection.has_string("error"sv))
        reply->complete(Error::from_string_literal("IndexedDB operation failed"));
    else
        reply->complete(move(inspection));
}

void WebContentPage::did_inspect_accessibility_tree(u64 request_id, String accessibility_tree)
{
    if (auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnAccessibilityTreeInspectionComplete>>(request_id))
        reply->complete(parse_json(accessibility_tree, "accessibility tree"sv));
}

void WebContentPage::did_get_accessibility_tree(u64 sequence, Vector<AccessibilityNodeData> nodes)
{
    if (!hosts_top_level_document())
        return;
    view().did_get_accessibility_tree(sequence, move(nodes));
}

void WebContentPage::did_change_accessibility_tree(u64 base_sequence, u64 sequence, AccessibilityTreeDelta delta)
{
    if (!hosts_top_level_document())
        return;
    view().did_change_accessibility_tree(base_sequence, sequence, move(delta));
}

void WebContentPage::did_accessibility_focus_change(i64 focused_node_id)
{
    if (!hosts_top_level_document())
        return;
    view().did_change_accessibility_focus(focused_node_id);
}

void WebContentPage::did_get_node_id_at_position(u64 request_id, Web::UniqueNodeID node_id)
{
    if (auto reply = take_reply_from_renderer<NodeHitTestReply>(request_id))
        reply->complete(node_id);
}

void WebContentPage::did_list_style_sheets(u64 request_id, Vector<Web::CSS::StyleSheetIdentifier> stylesheets)
{
    if (auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnStyleSheetsReceived>>(request_id))
        reply->complete(move(stylesheets));
}

void WebContentPage::did_get_style_sheet_source(Web::CSS::StyleSheetIdentifier identifier, URL::URL base_url, Utf16String source)
{
    if (!hosts_top_level_document())
        return;
    if (view().on_received_style_sheet_source)
        view().on_received_style_sheet_source(identifier, base_url, source);
}

void WebContentPage::did_list_devtools_sources(u64 request_id, Vector<Web::HTML::ScriptRegistryDescription> sources)
{
    if (auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnSourcesReceived>>(request_id))
        reply->complete(move(sources));
}

void WebContentPage::did_get_devtools_source(u64 request_id, Web::HTML::ScriptRegistryIdentifier, Optional<Web::HTML::ScriptRegistryContent> source)
{
    auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnSourceReceived>>(request_id);
    if (!reply)
        return;
    if (source.has_value())
        reply->complete(source.release_value());
    else
        reply->complete(Error::from_string_literal("Unable to locate source"));
}

void WebContentPage::did_add_devtools_source(Web::HTML::ScriptRegistryDescription source)
{
    if (!hosts_top_level_document())
        return;
    if (view().on_devtools_source_available)
        view().on_devtools_source_available(move(source));
}

void WebContentPage::did_pause_debugger(DebuggerPause pause)
{
    if (!hosts_top_level_document())
        return;
    view().did_pause_debugger(move(pause));
}

void WebContentPage::did_resume_debugger()
{
    if (!hosts_top_level_document())
        return;
    view().did_resume_debugger();
}

void WebContentPage::did_complete_debugger_breakpoint_operation(u64 request_id, Optional<String> error)
{
    auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnDebuggerBreakpointOperationComplete>>(request_id);
    if (!reply)
        return;
    if (error.has_value())
        reply->complete(Error::from_string_literal("Debugger breakpoint operation failed"));
    else
        reply->complete({});
}

void WebContentPage::did_take_screenshot(u64 request_id, Gfx::ShareableBitmap screenshot)
{
    if (auto reply = take_reply_from_renderer<ScreenshotReply>(request_id))
        reply->complete(move(screenshot));
}

void WebContentPage::did_get_internal_page_info(u64 request_id, WebView::PageInfoType, Optional<Core::AnonymousBuffer> info)
{
    if (auto reply = take_reply_from_renderer<PageInfoReply>(request_id))
        reply->complete(move(info));
}

void WebContentPage::did_get_selected_text(u64 request_id, ByteString selection)
{
    if (auto reply = take_reply_from_renderer<PendingReply<ErrorOr<ByteString>>>(request_id))
        reply->complete(move(selection));
}

void WebContentPage::did_get_selected_text_for_lookup(u64 request_id, Optional<DictionaryLookup> lookup)
{
    // The page hosting the tab's focused navigable gives the baseline origin in the viewport of its local root.
    if (lookup.has_value() && lookup->baseline_origin.has_value())
        lookup->baseline_origin->translate_by(traversable().focused_navigable_host_offset().to_type<int>());
    if (auto reply = take_reply_from_renderer<PendingReply<ErrorOr<Optional<DictionaryLookup>>>>(request_id))
        reply->complete(move(lookup));
}

void WebContentPage::did_select_word_for_dictionary_lookup(u64 request_id, bool selected)
{
    if (auto reply = take_reply_from_renderer<PendingReply<ErrorOr<bool>>>(request_id))
        reply->complete(selected);
}

void WebContentPage::did_cut_selected_text(u64 request_id, ByteString selection)
{
    if (auto reply = take_reply_from_renderer<PendingReply<ErrorOr<ByteString>>>(request_id))
        reply->complete(move(selection));
}

void WebContentPage::did_execute_js_console_input(u64 request_id, DebuggerValue result)
{
    if (auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnScriptEvaluationComplete>>(request_id))
        reply->complete(move(result));
}

void WebContentPage::did_output_js_console_message(ConsoleOutput console_output)
{
    if (!hosts_top_level_document())
        return;
    if (view().on_console_message)
        view().on_console_message(move(console_output));
}

void WebContentPage::did_block_resource(URL::URL url, String kind)
{
    traversable().presentation().did_block_resource(id(), url, kind);
}

void WebContentPage::did_start_network_request(u64 request_id, URL::URL url, ByteString method, Vector<HTTP::Header> request_headers, ByteBuffer request_body, Optional<String> initiator_type, String referrer_policy, bool is_navigation_request, Web::Fetch::Infrastructure::RequestPriority priority)
{
    if (!hosts_top_level_document())
        return;
    if (view().on_network_request_started)
        view().on_network_request_started(request_id, url, method, request_headers, move(request_body), move(initiator_type), move(referrer_policy), is_navigation_request, priority);
}

void WebContentPage::did_receive_network_response_body(u64 request_id, ByteBuffer data)
{
    if (!hosts_top_level_document())
        return;
    if (view().on_network_response_body_received)
        view().on_network_response_body_received(request_id, move(data));
}

void WebContentPage::did_finish_network_request(u64 request_id, u64 body_size, Requests::RequestTimingInfo timing_info, Optional<Requests::NetworkError> network_error)
{
    if (!hosts_top_level_document())
        return;
    if (view().on_network_request_finished)
        view().on_network_request_finished(request_id, body_size, timing_info, network_error);
}

void WebContentPage::did_request_set_prompt_text(Utf16String message)
{
    if (view().on_request_set_prompt_text)
        view().on_request_set_prompt_text(message);
}

void WebContentPage::did_request_accept_dialog()
{
    if (view().on_request_accept_dialog)
        view().on_request_accept_dialog();
}

void WebContentPage::did_request_dismiss_dialog()
{
    if (view().on_request_dismiss_dialog)
        view().on_request_dismiss_dialog();
}

void WebContentPage::did_request_document_cookie_version_index(Web::HTML::EnvironmentId environment_id, i64 document_id, String domain)
{
    auto environment = hosted_environment(environment_id);
    if (!environment.has_value() || environment->origin().is_opaque() || domain != environment->origin().host().serialize().to_ascii_lowercase())
        return;

    if (hosts_top_level_document()) {
        if (auto document_index = traversable().ensure_document_cookie_version_index(domain); !document_index.is_error())
            async_set_document_cookie_version_index(document_id, document_index.value());
    }
}

void WebContentPage::clear_storage_for_testing(Web::StorageAPI::StorageEndpointType storage_endpoint, Web::HTML::EnvironmentId const& environment_id)
{
    auto canonical_key = canonical_storage_key(environment_id);
    auto* storage_jar = this->storage_jar(storage_endpoint);
    if (canonical_key.has_value() && storage_jar)
        storage_jar->clear_storage_key(storage_endpoint, *canonical_key);
}

Messages::WebContentClient::ConnectToStorageAreaResponse WebContentPage::connect_to_storage_area(Web::StorageAPI::StorageEndpointType storage_endpoint, Web::HTML::EnvironmentId environment_id)
{
    auto canonical_key = canonical_storage_key(environment_id);
    auto* storage_jar = this->storage_jar(storage_endpoint);
    if (!canonical_key.has_value() || !storage_jar)
        return { {}, {}, {}, 0 };

    auto connection = StorageAreaRegistry::the().connect(client(), *storage_jar, storage_endpoint, *canonical_key);
    if (!connection.has_value())
        return { {}, {}, {}, 0 };

    Vector<Utf16String> keys;
    Vector<Utf16String> values;
    keys.ensure_capacity(connection->items.size());
    values.ensure_capacity(connection->items.size());
    for (auto& item : connection->items) {
        keys.unchecked_append(move(item.key));
        values.unchecked_append(move(item.value));
    }
    return { connection->id, move(keys), move(values), connection->usage };
}

void WebContentPage::did_change_needs_beforeunload_check(bool needs_beforeunload_check)
{
    m_needs_beforeunload_check = needs_beforeunload_check;
}

// https://html.spec.whatwg.org/multipage/interaction.html#transient-activation-duration
static constexpr auto transient_activation_duration = AK::Duration::from_seconds(5);

void WebContentPage::give_user_activation()
{
    m_last_user_activation = MonotonicTime::now();
}

void WebContentPage::notify_about_user_activation(Web::HTML::CrossProcessId document_id)
{
    give_user_activation();
    async_notify_about_user_activation(document_id);
}

bool WebContentPage::has_transient_user_activation() const
{
    // NB: Automation lets a renderer synthesize the input that gives it activation.
    if (WebContentClient::renderers_may_synthesize_input())
        return true;
    if (m_last_user_activation.has_value() && MonotonicTime::now() - *m_last_user_activation < transient_activation_duration)
        return true;
    return view().is_delivering_user_activation_to(*this);
}

// NB: The UI process consumes its activation only where the renderer consumes the activation of its windows too, so
//     the two agree on what is left of it. As the renderer does, it consumes the activation every page of the tab has
//     from input they finished handling, along with that of the input the page is handling, which the renderer gave
//     the activation it consumes. A page whose activation comes only from input it has yet to finish handling consumes
//     that of the earliest such input. Input queued behind keeps its activation either way, as the renderer gives
//     activation anew for each press it handles.
bool WebContentPage::consume_transient_user_activation()
{
    if (!has_transient_user_activation())
        return false;

    auto has_activation_of_handled_input = m_last_user_activation.has_value() && MonotonicTime::now() - *m_last_user_activation < transient_activation_duration;
    auto& top_level_traversable = traversable().top_level_traversable();
    top_level_traversable.for_each_hosting_page([](WebContentPage& page) {
        page.m_last_user_activation.clear();
    });
    if (has_activation_of_handled_input)
        top_level_traversable.view().consume_user_activation_of_input_handled_by(*this);
    else
        top_level_traversable.view().consume_user_activation_of_input_in_flight_to(*this);
    return true;
}

void WebContentPage::did_consume_user_activation(Web::HTML::UserActivationConsumption consumption)
{
    auto& page_host = this->traversable();
    page_host.top_level_traversable().for_each_hosting_page([&](WebContentPage& page) {
        if (&page == this)
            return;
        page.async_consume_user_activation(consumption);
    });
}

void WebContentPage::webdriver_user_prompt_handling_complete(u64 request_id, Web::WebDriver::Response response)
{
    if (auto reply = take_reply_from_renderer<WebDriverResponseReply>(request_id))
        reply->complete(move(response));
}

void WebContentPage::webdriver_did_set_current_browsing_context(u64 command_id, Web::HTML::CrossProcessId navigable_id)
{
    view().did_set_webdriver_current_browsing_context(command_id, navigable_id);
}

void WebContentPage::webdriver_command_complete(u64 command_id, Web::WebDriver::Response response)
{
    view().did_complete_webdriver_content_command(*this, command_id, move(response));
}

void WebContentPage::webdriver_bidi_event(Web::HTML::CrossProcessId navigable_id, String name, JsonValue params)
{
    if (auto* webdriver_bidi = WebDriverBiDi::the())
        webdriver_bidi->did_receive_event(*this, navigable_id, name, move(params));
}

void WebContentPage::webdriver_bidi_navigation_event(Web::HTML::CrossProcessId navigable_id, String name, Optional<Utf16String> navigation_id, URL::URL url)
{
    if (auto* webdriver_bidi = WebDriverBiDi::the())
        webdriver_bidi->did_receive_navigation_event(*this, navigable_id, name, move(navigation_id), move(url));
}

void WebContentPage::did_update_resource_count(i32 count_waiting)
{
    traversable().presentation().did_update_resource_count(*this, count_waiting);
}

// Only WebDriver's window commands and the internals object of a test ask for a change to the window.
static bool may_change_window(WebContentPage& page, StringView message)
{
    if (WebContentClient::renderers_may_synthesize_input())
        return true;
    page.client().did_misbehave(message, "not driven by WebDriver or a test"sv);
    return false;
}

void WebContentPage::did_request_restore_window()
{
    if (!may_change_window(*this, "did_request_restore_window"sv))
        return;
    if (view().on_restore_window)
        view().on_restore_window();
}

void WebContentPage::did_request_reposition_window(Gfx::IntPoint position, u64 completion_id)
{
    if (!may_change_window(*this, "did_request_reposition_window"sv))
        return;
    auto reply = PendingReply<>::create([page = NonnullRefPtr { *this }, completion_id] {
        page->async_did_complete_window_rect_request(completion_id);
    });
    if (auto* view = expect_reply_from_ui(reply)) {
        if (view->on_reposition_window)
            view->on_reposition_window(position);
        reply->complete();
    }
}

void WebContentPage::did_request_resize_window(Gfx::IntSize size, u64 completion_id)
{
    if (!may_change_window(*this, "did_request_resize_window"sv))
        return;
    auto reply = PendingReply<>::create([page = NonnullRefPtr { *this }, completion_id] {
        page->async_did_complete_window_rect_request(completion_id);
    });
    if (auto* view = expect_reply_from_ui(reply)) {
        if (view->on_resize_window)
            view->on_resize_window(size);
        reply->complete();
    }
}

void WebContentPage::did_request_maximize_window(u64 completion_id)
{
    if (!may_change_window(*this, "did_request_maximize_window"sv))
        return;
    auto reply = PendingReply<>::create([page = NonnullRefPtr { *this }, completion_id] {
        page->async_did_complete_window_rect_request(completion_id);
    });
    if (auto* view = expect_reply_from_ui(reply)) {
        if (view->on_maximize_window)
            view->on_maximize_window();
        reply->complete();
    }
}

void WebContentPage::did_request_minimize_window()
{
    if (!may_change_window(*this, "did_request_minimize_window"sv))
        return;
    if (view().on_minimize_window)
        view().on_minimize_window();
}

void WebContentPage::did_request_fullscreen_window()
{
    auto reply = PendingReply<bool>::create([page = NonnullRefPtr { *this }](bool accepted) {
        if (!accepted)
            page->async_deny_fullscreen_window();
    },
        false);
    if (auto* view = expect_reply_from_ui(reply)) {
        if (!consume_transient_user_activation() || !view->on_fullscreen_window) {
            reply->cancel();
            return;
        }
        view->on_fullscreen_window();
        reply->complete(true);
    }
}

void WebContentPage::did_request_exit_fullscreen()
{
    if (view().on_exit_fullscreen_window)
        view().on_exit_fullscreen_window();
}

void WebContentPage::did_exit_picture_in_picture()
{
    PictureInPictureManager::the().close_window(*this);
}

void WebContentPage::did_change_picture_in_picture_video_size(Gfx::IntSize video_size)
{
    PictureInPictureManager::the().video_size_did_change(*this, video_size);
}

void WebContentPage::did_change_picture_in_picture_player_controls_visibility(bool shown)
{
    view().did_change_picture_in_picture_player_controls_visibility(shown);
}

// A navigation's population steps run in the process with its source document, which need not have hosted local file
// content. For a navigation to a file: URL the UI process admitted, that process reads the file the URL names, as it does
// for a history step to an entry of one.
bool WebContentPage::populates_a_navigation_to_local_file(ByteString const& path) const
{
    if (traversable().page_populates_a_history_entry_for_local_file(*this, path))
        return true;

    bool result = false;
    traversable().for_each_in_inclusive_subtree([&](CanonicalNavigable const& navigable) {
        auto navigation = navigable.uncommitted_navigation();
        result = navigation
            && navigation->worker() == this
            && navigation->url.scheme() == "file"sv
            && navigation->url.file_path() == path;
        return result ? IterationDecision::Break : IterationDecision::Continue;
    });
    return result;
}

void WebContentPage::did_request_file(ByteString path, i32 request_id)
{
    // NB: The renderer's sandbox keeps it out of the local file system, so this is the only way it can read a file.
    //     Answer only a process that has hosted local file content, or one reading the file a navigation it populates
    //     names: any other could read every file the user can.
    if (!client().may_read_local_files() && !populates_a_navigation_to_local_file(path)) {
        async_handle_file_return(EACCES, {}, request_id);
        return;
    }

    auto file = open_local_file_for_renderer(path);
    if (file.is_error())
        async_handle_file_return(file.error().code(), {}, request_id);
    else
        async_handle_file_return(0, file.release_value(), request_id);
}

void WebContentPage::did_request_color_picker(Color current_color)
{
    auto reply = ColorPickerReply::create([page = NonnullRefPtr { *this }](Optional<Color> color) {
        page->async_color_picker_update(color, Web::HTML::ColorPickerUpdateState::Closed);
    },
        {});
    reply->set_on_update([page = NonnullRefPtr { *this }](Optional<Color> color) {
        page->async_color_picker_update(color, Web::HTML::ColorPickerUpdateState::Update);
    });
    if (auto* view = expect_reply_from_ui(reply))
        view->request_color_picker(current_color, move(reply));
}

void WebContentPage::did_request_date_picker(Utf16String input_type, Utf16String value, Utf16String minimum, Utf16String maximum)
{
    auto reply = DatePickerReply::create([page = NonnullRefPtr { *this }](Optional<Utf16String> value) {
        page->async_date_picker_closed(move(value));
    },
        {});
    if (auto* view = expect_reply_from_ui(reply))
        view->request_date_picker(input_type, value, minimum, maximum, move(reply));
}

static Optional<Web::Permissions::PermissionName> permission_name_from(Utf16String const& name)
{
    if (name == "geolocation"_utf16)
        return Web::Permissions::PermissionName::Geolocation;
    if (name == "microphone"_utf16)
        return Web::Permissions::PermissionName::Microphone;
    if (name == "notifications"_utf16)
        return Web::Permissions::PermissionName::Notifications;
    return {};
}

// The policy-controlled feature that gates a permission, for the permissions that have one.
static Optional<Web::PermissionsPolicy::PolicyControlledFeature> policy_controlled_feature_for(Web::Permissions::PermissionName name)
{
    switch (name) {
    case Web::Permissions::PermissionName::Geolocation:
        return Web::PermissionsPolicy::PolicyControlledFeature::Geolocation;
    case Web::Permissions::PermissionName::Microphone:
        return Web::PermissionsPolicy::PolicyControlledFeature::Microphone;
    case Web::Permissions::PermissionName::Notifications:
        return {};
    }
    VERIFY_NOT_REACHED();
}

bool WebContentPage::hosts_a_document_allowed_to_use(Web::PermissionsPolicy::PolicyControlledFeature feature) const
{
    auto is_allowed = false;
    for_each_navigable_with_a_hosted_active_document([&](CanonicalNavigable const& navigable) {
        is_allowed = navigable.active_document().is_allowed_to_use(feature);
        return is_allowed ? IterationDecision::Break : IterationDecision::Continue;
    });
    return is_allowed;
}

// Unlike for_each_hosted_document(), this skips the documents populated for navigations, which are not active yet.
void WebContentPage::for_each_navigable_with_a_hosted_active_document(Function<IterationDecision(CanonicalNavigable const&)> const& callback) const
{
    traversable().for_each_in_inclusive_subtree([&](CanonicalNavigable const& navigable) {
        if (!hosts_active_document_of(navigable))
            return IterationDecision::Continue;
        return callback(navigable);
    });
}

// Notifications has no policy-controlled feature, so the frame check is ours: only a document whose origin is the
// top-level origin, the site the user sees in the location bar, gets to ask for it or to show one.
static bool permission_reaches_document(Web::Permissions::PermissionName name, CanonicalDocument const& document, URL::Origin const& top_level_origin)
{
    if (name != Web::Permissions::PermissionName::Notifications)
        return true;
    return document.origin().is_same_origin(top_level_origin);
}

bool WebContentPage::document_may_use(Web::HTML::CrossProcessId document_id, Web::Permissions::PermissionName name) const
{
    auto document = hosted_document_with_id(document_id);
    if (!document)
        return false;
    auto top_level_origin = document->top_level_origin();
    if (!top_level_origin.has_value())
        return false;
    if (auto feature = policy_controlled_feature_for(name); feature.has_value() && Web::PermissionsPolicy::check_permissions_policy(document->permissions_policy(), *feature, document->origin(), document->origin()) != Web::PermissionsPolicy::FeatureValue::Enabled)
        return false;
    if (!permission_reaches_document(name, *document, *top_level_origin))
        return false;
    auto& manager = *client().session().permission_manager;
    if (top_level_origin->is_opaque()) {
        // A file: page's grant lasts as long as the page, but the global default still binds it.
        if (manager.global_default(name) == Web::Permissions::EffectiveState::Denied)
            return false;
        return m_file_permission_grants.find_first_index_if([&](auto const& grant) { return grant.document_id == document_id && grant.name == name; }).has_value();
    }
    return manager.effective_state(name, *top_level_origin) == Web::Permissions::EffectiveState::Granted;
}

void WebContentPage::did_request_geolocation_position(u64 request_id, Web::HTML::CrossProcessId document_id)
{
    // The UI process hands out a position only to a document it knows the user let have one.
    if (!document_may_use(document_id, Web::Permissions::PermissionName::Geolocation)) {
        async_geolocation_position_response(request_id, {}, to_underlying(Web::Geolocation::GeolocationPositionErrorCode::PermissionDenied));
        return;
    }
    view().request_geolocation_position(*this, request_id, document_id);
}

bool WebContentPage::hosts_a_document_with_top_level_origin(URL::Origin const& origin) const
{
    auto hosts = false;
    for_each_navigable_with_a_hosted_active_document([&](CanonicalNavigable const& navigable) {
        auto top_level_origin = navigable.active_document().top_level_origin();
        hosts = top_level_origin.has_value() && top_level_origin->is_same_origin(origin);
        return hosts ? IterationDecision::Break : IterationDecision::Continue;
    });
    return hosts;
}

RefPtr<CanonicalDocument> WebContentPage::hosted_document_with_id(Web::HTML::CrossProcessId document_id) const
{
    RefPtr<CanonicalDocument> found;
    traversable().for_each_in_inclusive_subtree([&](CanonicalNavigable const& navigable) {
        auto document = navigable.document_with_id(document_id);
        if (!document || document->host() != this)
            return IterationDecision::Continue;
        found = move(document);
        return IterationDecision::Break;
    });
    return found;
}

void WebContentPage::did_request_permission(u64 request_id, Web::HTML::CrossProcessId document_id, Utf16String name)
{
    auto renderer_reply = PendingReply<Web::Permissions::EffectiveState>::create([page = NonnullRefPtr { *this }, request_id](auto state) {
        page->async_permission_request_completed(request_id, state);
    },
        Web::Permissions::EffectiveState::Denied);
    if (!expect_reply_from_ui(renderer_reply))
        return;
    auto permission_name = permission_name_from(name);
    if (!permission_name.has_value()) {
        client().did_misbehave("did_request_permission"sv, "unknown permission name"sv);
        return;
    }

    auto deny = [&] { renderer_reply->complete(Web::Permissions::EffectiveState::Denied); };

    view().did_request_permission(*permission_name);

    // The UI process decides from what it knows of the requesting document, never from what the renderer says of it.
    auto document = hosted_document_with_id(document_id);
    if (!document) {
        deny();
        return;
    }
    auto top_level_origin = document->top_level_origin();
    if (!top_level_origin.has_value()) {
        deny();
        return;
    }

    if (auto feature = policy_controlled_feature_for(*permission_name); feature.has_value()) {
        auto policy = Web::PermissionsPolicy::check_permissions_policy(document->permissions_policy(), *feature, document->origin(), document->origin());
        if (policy != Web::PermissionsPolicy::FeatureValue::Enabled) {
            deny();
            return;
        }
    }
    if (!permission_reaches_document(*permission_name, *document, *top_level_origin)) {
        deny();
        return;
    }

    // A file: page has an opaque origin, and it's the only one that gets to ask. Its answer lasts as long as the page.
    auto is_file = top_level_origin->is_opaque() && traversable().active_document_url().scheme() == "file"sv;
    if (!is_file) {
        auto is_trustworthy = [](URL::Origin const& origin) {
            return Web::SecureContexts::is_origin_potentially_trustworthy(origin) == Web::SecureContexts::Trustworthiness::PotentiallyTrustworthy;
        };
        if (!is_trustworthy(*top_level_origin) || !is_trustworthy(document->origin())) {
            deny();
            return;
        }
    }

    auto& manager = *client().session().permission_manager;
    if (!is_file) {
        if (auto state = manager.effective_state(*permission_name, *top_level_origin); state != Web::Permissions::EffectiveState::Prompt) {
            renderer_reply->complete(state);
            return;
        }
    } else if (manager.global_default(*permission_name) == Web::Permissions::EffectiveState::Denied) {
        // The global default binds a file: page too; its own grant only matters while sites may ask.
        deny();
        return;
    }

    // A page asks for notifications only from a gesture, and the UI process holds it to the activation it delivered
    // itself, consuming it as the renderer does: a renderer that skipped its own check gets no prompt.
    if (*permission_name == Web::Permissions::PermissionName::Notifications && !consume_transient_user_activation()) {
        deny();
        return;
    }

    // Allowing the site the microphone the system keeps from the browser would get it nothing, so the user is told why
    // instead of being asked. Nothing is decided for the site, which may ask again once the system relents.
    if (*permission_name == Web::Permissions::PermissionName::Microphone && view().microphone_authorization() == MicrophoneAuthorization::Denied) {
        view().show_microphone_denied_notice();
        renderer_reply->complete(Web::Permissions::EffectiveState::Prompt);
        return;
    }

    auto generation_when_asked = manager.decision_generation(*permission_name, *top_level_origin);
    // When the system hasn't yet let the browser use the feature either, the prompt says the system will ask next.
    auto show_os_hint = [&] {
        switch (*permission_name) {
        case Web::Permissions::PermissionName::Geolocation:
            return Application::the().geolocation_authorization() == Core::GeolocationAuthorization::NotDetermined;
        case Web::Permissions::PermissionName::Notifications:
            return NotificationManager::the().backend().os_authorization_is_undetermined();
        case Web::Permissions::PermissionName::Microphone:
            return view().microphone_authorization() == MicrophoneAuthorization::NotDetermined;
        }
        VERIFY_NOT_REACHED();
    }();
    PermissionPromptRequest request { *permission_name, *top_level_origin, is_file, show_os_hint };

    auto reply = PermissionPromptReply::create([page = NonnullRefPtr(*this), renderer_reply, document_id, permission_name = *permission_name, top_level_origin = *top_level_origin, is_file, generation_when_asked](Optional<Web::Permissions::PromptOutcome> response) {
        if (!page->is_open())
            return;

        // With no one to ask, the request is denied without deciding anything for the site.
        if (!response.has_value()) {
            renderer_reply->complete(Web::Permissions::EffectiveState::Denied);
            return;
        }
        auto outcome = *response;

        auto state = Web::Permissions::EffectiveState::Prompt;
        if (is_file) {
            if (outcome == Web::Permissions::PromptOutcome::AllowAlways || outcome == Web::Permissions::PromptOutcome::AllowOnce)
                state = Web::Permissions::EffectiveState::Granted;
            else if (outcome == Web::Permissions::PromptOutcome::Block)
                state = Web::Permissions::EffectiveState::Denied;
            if (state == Web::Permissions::EffectiveState::Granted)
                page->m_file_permission_grants.append({ document_id, permission_name });
            if (state == Web::Permissions::EffectiveState::Granted && permission_name == Web::Permissions::PermissionName::Microphone) {
                if (auto result = page->client().grant_audio_capture_to_file(page->traversable().active_document_url()); result.is_error())
                    warnln("Unable to grant audio capture: {}", result.error());
            }
        } else {
            auto& manager = *page->client().session().permission_manager;

            // The answer counts only if nothing was decided for the site while the prompt was up: A decision made
            // elsewhere (in the site-info panel, e.g.) wins, and a document that's gone takes its prompt with it. And
            // requests that shared one prompt record its answer once, as the first of them records it.
            auto still_relevant = page->hosted_document_with_id(document_id) && manager.decision_generation(permission_name, top_level_origin) == generation_when_asked;
            if (still_relevant)
                manager.record_prompt_outcome(permission_name, top_level_origin, outcome);
            state = manager.effective_state(permission_name, top_level_origin);
        }

        // The AudioServer hears of the permission before the page does, so the page's first request finds it in place.
        auto is_microphone = permission_name == Web::Permissions::PermissionName::Microphone;
        if (is_microphone)
            page->client().update_audio_capture_permission();

        if (!is_microphone || state != Web::Permissions::EffectiveState::Granted) {
            renderer_reply->complete(state);
            return;
        }

        // The system may only now ask the user for the microphone. If it keeps it from the browser, the site keeps the
        // user's answer for when it relents, but this request gets nothing.
        auto microphone_reply = MicrophoneAuthorizationReply::create([page, renderer_reply, document_id](bool authorized) {
            if (!page->is_open())
                return;
            // The user may have taken the permission away while the system asked.
            auto granted = authorized && page->document_may_use(document_id, Web::Permissions::PermissionName::Microphone);
            renderer_reply->complete(granted ? Web::Permissions::EffectiveState::Granted : Web::Permissions::EffectiveState::Prompt);
        },
            false);
        if (auto* view = page->expect_reply_from_ui(microphone_reply))
            view->ensure_microphone_authorization(move(microphone_reply));
    },
        {});
    if (auto* view = expect_reply_from_ui(reply))
        view->request_permission_prompt(move(request), move(reply));
}

Web::Permissions::PermissionSnapshot WebContentPage::set_permission_for_testing(Web::HTML::CrossProcessId document_id, String const& name, String const& state)
{
    auto& manager = *client().session().permission_manager;
    auto permission_name = Web::Permissions::permission_name_from_string(name);
    auto document = hosted_document_with_id(document_id);
    auto top_level_origin = document ? document->top_level_origin() : Optional<URL::Origin> {};

    if (permission_name.has_value() && top_level_origin.has_value()) {
        if (state == "granted"sv)
            manager.set_decision(*permission_name, *top_level_origin, PermissionDecision::Allow);
        else if (state == "denied"sv)
            manager.set_decision(*permission_name, *top_level_origin, PermissionDecision::Block);
        else if (state == "prompt"sv)
            manager.set_decision(*permission_name, *top_level_origin, {});
        if (*permission_name == Web::Permissions::PermissionName::Microphone)
            client().update_audio_capture_permission();
        client().add_permission_origin(*top_level_origin);
    }
    return manager.snapshot_for(client().permission_origins());
}

void WebContentPage::set_next_permission_prompt_outcome_for_testing(String const& name, String const& outcome, bool held)
{
    auto permission_name = Web::Permissions::permission_name_from_string(name);
    if (!permission_name.has_value())
        return;

    Optional<Web::Permissions::PromptOutcome> prompt_outcome;
    if (outcome == "allow"sv)
        prompt_outcome = Web::Permissions::PromptOutcome::AllowAlways;
    else if (outcome == "allow-once"sv)
        prompt_outcome = Web::Permissions::PromptOutcome::AllowOnce;
    else if (outcome == "block"sv)
        prompt_outcome = Web::Permissions::PromptOutcome::Block;
    else if (outcome == "dismiss"sv)
        prompt_outcome = Web::Permissions::PromptOutcome::Dismissed;
    else if (outcome == "ignore"sv)
        prompt_outcome = Web::Permissions::PromptOutcome::Ignored;
    if (prompt_outcome.has_value())
        view().set_next_permission_prompt_outcome_for_testing(*permission_name, *prompt_outcome, held);
}

static Optional<MicrophoneAuthorization> microphone_authorization_from_string(StringView string)
{
    if (string == "not-determined"sv)
        return MicrophoneAuthorization::NotDetermined;
    if (string == "authorized"sv)
        return MicrophoneAuthorization::Authorized;
    if (string == "denied"sv)
        return MicrophoneAuthorization::Denied;
    return {};
}

void WebContentPage::set_system_microphone_authorization_for_testing(String const& authorization, String const& answer, bool held)
{
    auto current = microphone_authorization_from_string(authorization);
    auto answered = microphone_authorization_from_string(answer);
    if (current.has_value() && answered.has_value())
        view().set_microphone_authorization_for_testing(*current, *answered, held);
}

void WebContentPage::release_system_microphone_answer_for_testing()
{
    view().release_system_microphone_answer_for_testing();
}

String WebContentPage::microphone_state_for_testing() const
{
    JsonObject state;
    state.set("noticesShown"sv, view().microphone_denied_notices_shown_for_testing());
    state.set("systemRequests"sv, view().system_microphone_requests_for_testing());
    state.set("captureDecisions"sv, client().audio_capture_decision_count());
    state.set("captureRefusals"sv, client().audio_capture_refusal_count());
    auto top_level_origin = traversable().active_document().top_level_origin().value_or(traversable().active_document_url().origin());
    state.set("dismissals"sv, client().session().permission_manager->dismissals_for_testing(Web::Permissions::PermissionName::Microphone, top_level_origin));
    return state.serialized();
}

void WebContentPage::release_held_permission_prompt_for_testing(u32 waiters)
{
    view().release_held_permission_prompt_for_testing(waiters);
}

Web::Permissions::PermissionSnapshot WebContentPage::advance_permission_clock_for_testing(u64 milliseconds)
{
    auto& manager = *client().session().permission_manager;
    manager.age_origin_for_testing(traversable().active_document_url().origin(), AK::Duration::from_milliseconds(static_cast<i64>(milliseconds)));
    return manager.snapshot_for(client().permission_origins());
}

void WebContentPage::did_request_show_notification(u64 notification_id, Web::HTML::CrossProcessId document_id, Web::NotificationsAPI::NotificationData data, Optional<Gfx::ShareableBitmap> icon)
{
    // The UI process shows a notification only for a document it knows the user let notify, read from its own store
    // as the notification arrives: A decision made while the page was still fetching the icon counts, and a renderer
    // that skipped its own check gets nothing.
    auto document = hosted_document_with_id(document_id);
    if (!document || !document_may_use(document_id, Web::Permissions::PermissionName::Notifications)) {
        async_notification_show_failed(notification_id);
        return;
    }

    RefPtr<Gfx::Bitmap const> icon_bitmap;
    if (icon.has_value() && icon->is_valid())
        icon_bitmap = icon->bitmap();
    NotificationManager::the().show(*this, notification_id, *document->top_level_origin(), move(data), move(icon_bitmap));
}

void WebContentPage::did_request_close_notification(u64 notification_id)
{
    NotificationManager::the().close(*this, notification_id);
}

void WebContentPage::did_abandon_notification(u64 notification_id)
{
    NotificationManager::the().abandon(*this, notification_id);
}

void WebContentPage::did_handle_notification_click(u64 notification_id, bool into_focus)
{
    NotificationManager::the().click_handled(*this, notification_id, into_focus);
}

void WebContentPage::did_request_click_notification_for_testing(u64 notification_id)
{
    auto* recording_backend = NotificationManager::the().backend().as_recording_backend();
    if (!recording_backend)
        return;
    if (auto id = NotificationManager::the().id_for(*this, notification_id); id.has_value())
        recording_backend->click(*id);
}

void WebContentPage::did_request_click_notification_by_id_for_testing(u64 browser_notification_id)
{
    if (auto* recording_backend = NotificationManager::the().backend().as_recording_backend())
        recording_backend->click(browser_notification_id);
}

void WebContentPage::did_request_dismiss_notification_for_testing(u64 notification_id)
{
    auto* recording_backend = NotificationManager::the().backend().as_recording_backend();
    if (!recording_backend)
        return;
    if (auto id = NotificationManager::the().id_for(*this, notification_id); id.has_value())
        recording_backend->dismiss(*id);
}

void WebContentPage::did_cancel_geolocation_position_request(u64 request_id)
{
    view().cancel_geolocation_position_request(*this, request_id);
}

void WebContentPage::did_start_geolocation_position_watch(u64 request_id, Web::HTML::CrossProcessId document_id)
{
    if (!document_may_use(document_id, Web::Permissions::PermissionName::Geolocation)) {
        async_geolocation_position_response(request_id, {}, to_underlying(Web::Geolocation::GeolocationPositionErrorCode::PermissionDenied));
        return;
    }
    view().start_geolocation_position_watch(*this, request_id, document_id);
}

void WebContentPage::did_stop_geolocation_position_watch(u64 request_id)
{
    view().stop_geolocation_position_watch(*this, request_id);
}

void WebContentPage::did_request_file_picker(Web::HTML::FileFilter accepted_file_types, Web::HTML::AllowMultipleFiles allow_multiple_files)
{
    auto reply = FilePickerReply::create([page = NonnullRefPtr { *this }](Vector<Web::HTML::SelectedFile> files) {
        page->async_file_picker_closed(move(files));
    },
        {});
    if (auto* view = expect_reply_from_ui(reply))
        view->request_file_picker(accepted_file_types, allow_multiple_files, move(reply));
}

void WebContentPage::did_finish_handling_input_event(u64 event_id, Web::EventResult event_result)
{
    relay_finished_input_event(*this, event_id, event_result);
}

void WebContentPage::relay_finished_input_event(WebContentPage const& reporting_page, u64 event_id, Web::EventResult event_result)
{
    if (hosts_top_level_document()) {
        view().did_finish_handling_input_event(reporting_page, event_id, event_result);
        return;
    }

    // The view displaying the tab handed the event down; it hears the result.
    if (auto display_page = traversable().display_page(); display_page) {
        if (display_page->is_open() && display_page.ptr() != this)
            display_page->relay_finished_input_event(reporting_page, event_id, event_result);
    }
}

void WebContentPage::did_update_input_method_state(Optional<Web::DevicePixelRect> caret_rect, bool is_enabled, i32 cursor_position, i32 anchor_position, Utf16String text_before_cursor, Utf16String text_after_cursor)
{

    // The page hosting the tab's focused navigable describes its text input, in the viewport of its local root.
    auto& traversable = this->traversable();
    auto host = traversable.focused_navigable_host();
    if (host != *this)
        return;
    if (caret_rect.has_value())
        caret_rect->translate_by(traversable.focused_navigable_host_offset());
    view().set_input_method_state({ is_enabled, cursor_position, anchor_position, move(text_before_cursor), move(text_after_cursor), caret_rect });
}

void WebContentPage::did_change_theme_color(Gfx::Color color)
{
    traversable().presentation().did_change_theme_color(*this, color);
}

void WebContentPage::did_change_background_color(Gfx::Color color)
{
    traversable().presentation().did_change_background_color(*this, color);
}

// NB: The system clipboard belongs to the user, so a page reaches it only right after the user's own input to it, as
//     with a copy or paste shortcut, a click on a page control, or the browser's Paste command.
void WebContentPage::did_insert_clipboard_item(Web::Clipboard::SystemClipboardItem item, String)
{
    if (!has_transient_user_activation())
        return;
    view().insert_clipboard_item(move(item));
}

void WebContentPage::set_audibility(Audio::Audibility audibility)
{
    if (m_audibility == audibility)
        return;
    m_audibility = audibility;
    if (m_traversable)
        m_traversable->presentation().update_audibility();
}

void WebContentPage::did_change_screen_wake_lock_state(Web::ScreenWakeLockState wake_lock_state)
{
    traversable().presentation().did_change_screen_wake_lock_state(*this, wake_lock_state);
}

void WebContentPage::did_start_using_gamepads()
{
    GamepadManager::the().page_did_start_using_gamepads(*this);
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
    // NB: The event enters the tab's input path as the user's own, reaching the focused navigable in whatever process
    //     hosts it and granting user activation, so only automation may ask for one.
    if (!WebContentClient::renderers_may_synthesize_input()) {
        client().did_misbehave("did_request_key_event_for_testing"sv, "not driven by WebDriver or a test"sv);
        return;
    }
    // Only the compositor sets these, on its way to WebContent.
    event.async_scroll_performed_default_action = false;
    event.user_input_scroll_count_with_nothing_to_scroll = {};
    view().enqueue_input_event(move(event));
}

void WebContentPage::request_history_operation(Web::HTML::CrossProcessId operation_id, Web::HistoryOperationParameters parameters)
{
    // Only the process hosting a navigable's active document appends its synchronous navigation steps.
    if (auto const* same_document = parameters.get_pointer<Web::FinalizeSameDocumentNavigationHistoryOperationParameters>(); same_document && !hosted_navigable(same_document->navigable_id).has_value()) {
        async_complete_history_operation(operation_id, {}, traversable().session_history().size());
        return;
    }
    if (auto const* resume = parameters.get_pointer<Web::ResumeTraverseHistoryOperationParameters>(); resume && !traversable().session_history().get_the_used_step(resume->target_step).has_value()) {
        client().did_misbehave("request_history_operation"sv, "resumed traversal to a step before every used step"sv);
        return;
    }
    traversable().enqueue_history_operation(operation_id, move(parameters), *this);
}

void WebContentPage::revoke_history_operation(Web::HTML::CrossProcessId operation_id)
{
    traversable().revoke_history_operation(*this, operation_id);
}

void WebContentPage::beforeunload_check_result(Web::HTML::CrossProcessId operation_id, Web::HTML::CheckIfUnloadingIsCanceledResult result, Web::HTML::UnloadPromptShown unload_prompt_shown)
{
    traversable().did_receive_beforeunload_check_result(*this, operation_id, result, unload_prompt_shown);
}

void WebContentPage::changing_navigable_history_job_reached_population(Web::HTML::CrossProcessId operation_id, Web::HTML::CrossProcessId navigable_id, Web::HTML::NavigationSourceSnapshot active_document_source_snapshot_params)
{
    traversable().did_receive_changing_navigable_history_job_reached_population(*this, operation_id, navigable_id, move(active_document_source_snapshot_params));
}

void WebContentPage::changing_navigable_history_job_ready(Web::HTML::CrossProcessId operation_id, Web::HTML::CrossProcessId navigable_id, Web::HTML::ChangingNavigableHistoryStepJobDisposition disposition, bool update_only)
{
    traversable().did_receive_changing_navigable_history_job_ready(*this, operation_id, navigable_id, disposition, update_only);
}

void WebContentPage::descendant_unload_task_complete(Web::HTML::CrossProcessId unload_id, Web::HTML::CrossProcessId navigable_id)
{
    traversable().did_receive_descendant_unload_task_complete(*this, unload_id, navigable_id);
}

void WebContentPage::request_navigable_document_abort(Web::HTML::CrossProcessId navigable_id)
{
    auto navigable = traversable().find(navigable_id);
    if (!navigable.has_value() || !holds_container_of(*navigable, "request_navigable_document_abort"sv))
        return;
    auto endpoint = endpoint_hosting_navigable_represented_by(navigable_id);
    if (!endpoint)
        return;
    endpoint->async_abort_navigable_document(navigable_id);
}

void WebContentPage::request_navigable_document_unfullscreen(Web::HTML::CrossProcessId navigable_id)
{
    auto endpoint = endpoint_hosting_navigable_represented_by(navigable_id);
    if (!endpoint)
        return;
    endpoint->async_unfullscreen_navigable_document(navigable_id);
}

// The container of a navigable is in the page hosting its parent, which runs the remaining steps of the request.
// The request waits in the page hosting the requesting navigable's document until every page above has answered.
void WebContentPage::request_navigable_container_fullscreen(Web::HTML::CrossProcessId navigable_id, Web::HTML::CrossProcessId requesting_navigable_id, Web::Fullscreen::RequestType request_type)
{
    if (auto host = page_hosting_container_of(navigable_id))
        host->async_fullscreen_navigable_container(navigable_id, requesting_navigable_id, request_type);
    else
        navigable_container_fullscreen_complete(requesting_navigable_id);
}

void WebContentPage::navigable_container_fullscreen_complete(Web::HTML::CrossProcessId requesting_navigable_id)
{
    if (auto host = page_hosting_navigable(requesting_navigable_id))
        host->async_container_fullscreen_complete(requesting_navigable_id);
}

void WebContentPage::request_navigable_container_unfullscreen(Web::HTML::CrossProcessId navigable_id)
{
    if (auto host = page_hosting_container_of(navigable_id))
        host->async_unfullscreen_navigable_container(navigable_id);
    else
        navigable_container_unfullscreen_complete(navigable_id);
}

void WebContentPage::navigable_container_unfullscreen_complete(Web::HTML::CrossProcessId navigable_id)
{
    if (auto host = page_hosting_navigable(navigable_id))
        host->async_container_unfullscreen_complete(navigable_id);
}

// The document of the tab's top-level traversable runs the steps, wherever a close request reached fullscreen.
void WebContentPage::request_fully_exit_fullscreen()
{
    view().exit_fullscreen();
}

RefPtr<WebContentPage> WebContentPage::page_hosting_container_of(Web::HTML::CrossProcessId navigable_id) const
{
    auto navigable = hosted_navigable(navigable_id);
    if (!navigable.has_value() || !navigable->parent())
        return {};
    auto host = traversable().page_hosting(*navigable->parent());
    if (!host || !host->is_open())
        return {};
    return host;
}

RefPtr<WebContentPage> WebContentPage::page_hosting_navigable(Web::HTML::CrossProcessId navigable_id) const
{
    auto navigable = traversable().find(navigable_id);
    if (!navigable.has_value())
        return {};
    auto host = traversable().page_hosting(*navigable);
    if (!host || !host->is_open())
        return {};
    return host;
}

void WebContentPage::request_child_navigable_unload(Web::HTML::CrossProcessId navigable_id)
{
    if (auto navigable = traversable().find(navigable_id); navigable.has_value() && !holds_container_of(*navigable, "request_child_navigable_unload"sv))
        return;
    traversable().did_receive_child_navigable_unload_request(*this, navigable_id);
}

void WebContentPage::changing_navigable_continuation_applied(Web::HTML::CrossProcessId operation_id, Web::HTML::CrossProcessId navigable_id, Optional<Web::HTML::HostedNavigableState> activated_navigable_state, Optional<Web::HTML::SessionHistoryEntryPersistedState> previous_entry_persisted_state)
{
    traversable().did_receive_changing_navigable_continuation_applied(*this, operation_id, navigable_id, move(activated_navigable_state), move(previous_entry_persisted_state));
}

void WebContentPage::nonchanging_navigable_history_state_updated(Web::HTML::CrossProcessId operation_id, Web::HTML::CrossProcessId navigable_id)
{
    traversable().did_receive_nonchanging_navigable_history_state_updated(*this, operation_id, navigable_id);
}

void WebContentPage::did_request_close_of_traversable(Web::HTML::CrossProcessId navigable_id, Web::HTML::CrossProcessId source_navigable_id, Web::HTML::NavigationSourceSnapshot source_snapshot)
{
    // window.close() from a document another process hosts: the process hosting the traversable's document runs the
    // rest of its steps.
    if (traversable().id() != navigable_id)
        return;

    // The process closing it must host the navigable it closes from. The request comes through the page holding the
    // traversable's tab, which is not the source's when the two are different tabs.
    Optional<CanonicalNavigable&> source;
    RefPtr<WebContentPage> source_page;
    client().for_each_page([&](WebContentPage& page) {
        source = page.hosted_navigable(source_navigable_id);
        if (!source.has_value())
            return IterationDecision::Continue;
        source_page = page;
        return IterationDecision::Break;
    });
    if (!source.has_value())
        return;

    // https://html.spec.whatwg.org/multipage/nav-history-apis.html#dom-window-close
    // 6. If all the following are true:
    //    - thisTraversable is script-closable;
    //    - the incumbent global object's browsing context is familiar with browsingContext; and
    //    - the incumbent global object's navigable is allowed by sandboxing to navigate thisTraversable, given sourceSnapshotParams,
    // NB: The requesting process found these true. The UI process checks them again with the state it holds, including
    //     the sandboxing flags of the incumbent global object's associated Document and the activation of the input it
    //     delivered to the page hosting it.
    Web::InitiatorSourceSnapshot source_snapshot_params {
        .sandboxing_flags = source_snapshot.sandboxing_flags | source->active_document().active_sandboxing_flag_set(),
        .has_transient_activation = source_snapshot.has_transient_activation && source_page->has_transient_user_activation(),
    };
    if (!traversable().is_script_closable() || !source->is_familiar_with(traversable()) || !source->allowed_by_sandboxing_to_navigate(traversable(), source_snapshot_params))
        return;

    auto endpoint = endpoint_hosting_navigable_represented_by(navigable_id);
    if (!endpoint)
        return;
    endpoint->async_close_traversable_from_script(navigable_id);
}

void WebContentPage::did_inspect_dom_tree(u64 request_id, String dom_tree)
{
    if (auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnTabInspectionComplete>>(request_id))
        reply->complete(parse_json(dom_tree, "DOM tree"sv));
}

void WebContentPage::did_inspect_dom_node(DOMNodeProperties properties)
{
    if (!hosts_top_level_document())
        return;
    if (view().on_received_dom_node_properties)
        view().on_received_dom_node_properties(move(properties));
}

void WebContentPage::did_finish_editing_dom_node(u64 request_id, Optional<Web::UniqueNodeID> node_id)
{
    auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnDOMNodeEditComplete>>(request_id);
    if (!reply)
        return;
    if (node_id.has_value())
        reply->complete(*node_id);
    else
        reply->complete(Error::from_string_literal("Unable to find DOM node to edit"));
}

void WebContentPage::did_mutate_dom(Mutation mutation)
{
    if (!hosts_top_level_document())
        return;
    if (view().on_dom_mutation_received)
        view().on_dom_mutation_received(move(mutation));
}

void WebContentPage::did_get_dom_node_html(u64 request_id, String html)
{
    if (auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnDOMNodeHTMLReceived>>(request_id))
        reply->complete(move(html));
}

void WebContentPage::did_resolve_dom_node_url(u64 request_id, String resolved_url)
{
    if (auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnResolvedURLReceived>>(request_id))
        reply->complete(move(resolved_url));
}

void WebContentPage::did_complete_js_console_input(u64 request_id, Optional<Vector<String>> matches, String match_prop)
{
    if (auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnConsoleInputCompleted>>(request_id))
        reply->complete(DevTools::DevToolsDelegate::ConsoleInputCompletion { move(matches), move(match_prop) });
}

void WebContentPage::did_receive_network_response_headers(u64 request_id, u32 status_code, Optional<String> reason_phrase, Vector<HTTP::Header> response_headers, Requests::CacheState cache_state)
{
    if (!hosts_top_level_document())
        return;
    if (view().on_network_response_headers_received)
        view().on_network_response_headers_received(request_id, status_code, reason_phrase, response_headers, cache_state);
}

void WebContentPage::did_update_indexed_database(String update)
{
    if (!hosts_top_level_document())
        return;
    view().notify_indexed_database_changed(parse_json(update, "IndexedDB update"sv));
}

void WebContentPage::did_request_clipboard_entries(u64 request_id)
{
    Vector<Web::Clipboard::SystemClipboardItem> items;
    if (!has_transient_user_activation()) {
        async_retrieved_clipboard_entries(request_id, items);
        return;
    }
    if (auto item = view().clipboard_item(); !item.system_clipboard_representations.is_empty())
        items.append(move(item));

    async_retrieved_clipboard_entries(request_id, items);
}

void WebContentPage::did_request_spell_check(u64 request_id, Utf16String text)
{
    auto reply = SpellCheckReply::create([page = NonnullRefPtr { *this }, request_id](Vector<Web::SpellingError> errors) {
        page->async_did_check_spelling(request_id, move(errors));
    },
        {});
    if (auto* view = expect_reply_from_ui(reply))
        view->request_spell_check(text, move(reply));
}

void WebContentPage::did_request_writing_suggestion(u64 request_id, Utf16String text, u32 caret_offset)
{
    auto reply = WritingSuggestionReply::create([page = NonnullRefPtr { *this }, request_id](Optional<Web::WritingSuggestion> suggestion) {
        page->async_did_get_writing_suggestion(request_id, move(suggestion));
    },
        {});
    if (auto* view = expect_reply_from_ui(reply))
        view->request_writing_suggestion(text, caret_offset, move(reply));
}

void WebContentPage::did_get_translation_language_sample(u64 request_id, Web::TranslationLanguageSample sample)
{
    view().page_translation().did_get_language_sample({}, request_id, move(sample));
}

void WebContentPage::did_request_translation(Web::TranslationUnit unit)
{
    view().page_translation().did_request_translation({}, *this, move(unit));
}

void WebContentPage::did_request_set_system_focus(bool has_system_focus)
{
    if (!may_change_window(*this, "did_request_set_system_focus"sv))
        return;
    traversable().set_has_system_focus(has_system_focus, *this);
}

void WebContentPage::did_request_set_system_visibility_state(Web::HTML::VisibilityState visibility_state)
{
    if (!may_change_window(*this, "did_request_set_system_visibility_state"sv))
        return;
    view().set_system_visibility_state(visibility_state);
}

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#navigate
// NB: initiatorOriginSnapshot is sourceDocument's origin. The process names sourceDocument's relevant settings object as
//     the fetch client, an environment that a process hosts, and the UI process takes the origin from it. Without a
//     sourceDocument, the process gives a new opaque origin, which no document may hold yet.
// The origin of the environment a navigation names as its fetch client, from the process asking, which hosts it.
static Optional<URL::Origin> initiator_origin_snapshot(URL::Origin const& given_origin, Web::HTML::NavigationSourceSnapshot const& source_snapshot_params, WebContentClient& hosting_client)
{
    if (!source_snapshot_params.fetch_client.has_value()) {
        if (!given_origin.is_opaque() || CanonicalTraversable::is_origin_held_by_a_document(given_origin))
            return {};
        return given_origin;
    }
    auto source_settings = hosting_client.hosted_environment(source_snapshot_params.fetch_client->id);
    if (!source_settings.has_value())
        return {};
    return source_settings->origin();
}

void WebContentPage::did_request_navigation_start(Web::HTML::CrossProcessId navigable_id, Web::NavigationTarget target, URL::URL url, Utf16String navigation_id, Optional<Web::HTML::NavigationStartRequest> start_request)
{
    CanonicalNavigable* target_navigable = &traversable();
    if (target == Web::NavigationTarget::IFrame) {
        auto child_frame = traversable().top_level_traversable().find(navigable_id);
        target_navigable = child_frame.has_value() ? &*child_frame : nullptr;
    }

    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#navigate
    // 18. If navigable's ongoing navigation is "traversal", then:
    //     2. Return.
    // NB: The page ran this step against the ongoing navigation it knew of, which the traversal queue can have set to
    //     "traversal" since.
    auto navigation_is_blocked_by_history_traversal = target_navigable
        && target_navigable->ongoing_navigation().has<CanonicalNavigable::Traversal>();
    Optional<URL::Origin> initiator_origin;
    if (start_request.has_value())
        initiator_origin = initiator_origin_snapshot(start_request->initiator_origin, start_request->source_snapshot_params, client());
    if (!target_navigable
        || target_navigable->id() != navigable_id
        || !traversable().hosts(*target_navigable, *this)
        || (start_request.has_value() && start_request->navigable_id != navigable_id)
        || (start_request.has_value() && !initiator_origin.has_value())
        || navigation_is_blocked_by_history_traversal) {
        async_cancel_navigation_params_creation(navigable_id, navigation_id);
        return;
    }
    // NB: Population takes the start request's URL, so it must be the URL this check and the admission record name.
    if (start_request.has_value() && start_request->url != url) {
        client().did_misbehave("did_request_navigation_start"sv, "start request names another URL"sv);
        return;
    }
    if (start_request.has_value() && url.scheme() == "file"sv && !may_navigate_to_a_local_file(client())) {
        async_cancel_navigation_params_creation(navigable_id, navigation_id);
        return;
    }
    if (start_request.has_value())
        start_request->initiator_origin = initiator_origin.release_value();

    if (!start_request.has_value()) {
        if (url.scheme() != "javascript"sv) {
            client().did_misbehave("did_request_navigation_start"sv, "navigation without a start request is not to a javascript: URL"sv);
            return;
        }
        target_navigable->begin_evaluating_javascript_url(url, navigation_id, *this);
        return;
    }

    auto retry = prepare_navigation_to_retry(*start_request);
    target_navigable->set_ongoing_navigation(CanonicalNavigation::Start {
        .url = move(url),
        .navigation_id = navigation_id,
        .retry = move(retry),
        .sequence_number = target_navigable->top_level_traversable().next_sequence_number(),
        .state = CanonicalNavigation::CheckingIfUnloadingIsCanceled { start_request.release_value() },
        .page = this,
    });
    begin_navigation_unload_check(*target_navigable, navigation_id);
}

// Navigate, step 23.1: checking if unloading is canceled for navigable's active document's inclusive descendant
// navigables. The pages hosting the documents this page, the population worker, does not run their checks first; this
// page's own, with the navigation's bookkeeping, comes last, told whether a prompt was shown.
void WebContentPage::begin_navigation_unload_check(CanonicalNavigable& target_navigable, Utf16String const& navigation_id)
{
    Vector<Web::HTML::CrossProcessId> inclusive_descendants;
    target_navigable.for_each_in_inclusive_subtree([&](CanonicalNavigable const& navigable) {
        inclusive_descendants.append(navigable.id());
        return IterationDecision::Continue;
    });
    (void)target_navigable.top_level_traversable().check_if_unloading_is_canceled(move(inclusive_descendants), {}, {}, *this, Web::HTML::UnloadPromptShown::No,
        [page = NonnullRefPtr<WebContentPage>(*this), navigable_id = target_navigable.id(), navigation_id](Web::HTML::CheckIfUnloadingIsCanceledResult result, Web::HTML::UnloadPromptShown unload_prompt_shown) {
            if (result != Web::HTML::CheckIfUnloadingIsCanceledResult::Continue) {
                // The navigation parked for its population is not coming; the recorded load ends as a failed one.
                page->async_cancel_navigation_params_creation(navigable_id, navigation_id);
                if (page->is_open())
                    page->did_fail_navigation_population(navigable_id, navigation_id);
                return;
            }
            page->async_run_navigation_unload_check(navigable_id, navigation_id, unload_prompt_shown);
        });
}

void WebContentPage::did_complete_navigation_unload_check(Web::HTML::CrossProcessId navigable_id, Utf16String navigation_id)
{
    auto navigable = population_worker_navigable(navigable_id);
    if (!navigable.has_value())
        return;

    auto ongoing_navigation = navigable->navigation_with_id(navigation_id);
    auto* checking = ongoing_navigation ? ongoing_navigation->state.get_pointer<CanonicalNavigation::CheckingIfUnloadingIsCanceled>() : nullptr;
    if (!checking || ongoing_navigation->worker() != this)
        return;

    auto population_request = Web::HTML::create_navigation_population_request(
        move(checking->start_request),
        Application::the().allocate_ui_process_cross_process_id());
    population_request.target_snapshot_params.sandboxing_flags |= navigable->snapshot_target_snapshot_params().sandboxing_flags;
    ongoing_navigation->history_entry = MUST(CanonicalSessionHistoryEntry::create_from_descriptor(Web::HTML::create_session_history_entry_descriptor(population_request.history_entry, 0)));
    ongoing_navigation->state = CanonicalNavigation::CreatingNavigationParams { NavigationLoader::create(client().is_private(), move(population_request)) };
    async_create_navigation_params(ongoing_navigation->loader()->request());

    // Requesting navigation params starts the fetch, so a view's top-level population begins its recorded
    // load here.
    if (navigable->is_top_level_traversable()) {
        if (hosts_top_level_document())
            begin_top_level_load(ongoing_navigation->loader()->request().history_entry.url);
    }
}

void WebContentPage::did_finish_navigation_params_creation(Web::HTML::CrossProcessId navigable_id, Utf16String navigation_id, Optional<Web::HTML::NavigationPopulationResult> result)
{
    auto navigable = population_worker_navigable(navigable_id);
    if (!navigable.has_value()) {
        if (result.has_value())
            NavigationLoader::discard(client().is_private(), *result);
        return;
    }

    auto ongoing_navigation = navigable->navigation_with_id(navigation_id);
    auto* creating = ongoing_navigation ? ongoing_navigation->state.get_pointer<CanonicalNavigation::CreatingNavigationParams>() : nullptr;
    if (!creating || ongoing_navigation->worker() != this) {
        if (result.has_value())
            NavigationLoader::discard(client().is_private(), *result);
        return;
    }

    if (!result.has_value()) {
        navigable->did_cancel_navigation(navigation_id);
        return;
    }

    // Steps 1-4 have produced final navigation params. Keep the pending entry in
    // sync with redirects before choosing the process that will run step 5.
    auto loader = move(creating->loader);
    ongoing_navigation->state = CanonicalNavigation::AcquiringResponseBody { move(loader) };
    NonnullRefPtr acquiring_loader = *ongoing_navigation->loader();
    acquiring_loader->did_finish_navigation_params_creation(result.release_value());
    if (ongoing_navigation->history_entry->update_from_descriptor(Web::HTML::create_session_history_entry_descriptor(acquiring_loader->request().history_entry, 0)).is_error()) {
        auto rejected_result = acquiring_loader->take_result();
        NavigationLoader::discard(client().is_private(), rejected_result);
        navigable->did_cancel_navigation(navigation_id);
        client().did_misbehave("did_finish_navigation_params_creation"sv, "replacement document state forms a cycle"sv);
        return;
    }
    ongoing_navigation->url = ongoing_navigation->history_entry->url;

    acquiring_loader->acquire_response_body([page = NonnullRefPtr<WebContentPage>(*this), navigable_id, navigation_id = move(navigation_id)](bool succeeded) {
        // The page may have closed while the body was fetched; its navigation then has nothing left to finish.
        if (!page->is_open())
            return;
        if (succeeded && page->continue_navigation_population_in_selected_process(navigable_id, navigation_id))
            return;
        if (auto navigable = page->population_worker_navigable(navigable_id); navigable.has_value())
            navigable->did_cancel_navigation(navigation_id);
    });
}

void WebContentPage::did_finish_history_navigation_params_creation(Web::HTML::CrossProcessId operation_id, Web::HTML::CrossProcessId navigable_id, Web::HTML::NavigationPopulationResult result)
{
    auto& host = this->traversable();
    host.top_level_traversable().did_finish_history_navigation_params_creation(*this, operation_id, navigable_id, move(result));
}

void WebContentPage::did_fail_navigation_population(Web::HTML::CrossProcessId navigable_id, Utf16String navigation_id)
{
    auto navigable = population_worker_navigable(navigable_id);
    if (!navigable.has_value())
        return;

    // The failure must name the admitted transaction, and must come from a process that owns part of its
    // outcome: the population worker (unload check, javascript: evaluation) or the population host.
    auto ongoing_navigation = navigable->navigation_with_id(navigation_id);
    if (!ongoing_navigation
        || ongoing_navigation->is_finalizing()
        || (ongoing_navigation->worker() != this && ongoing_navigation->host() != this)) {
        return;
    }

    // Only a failed population handoff owns the loader's response body.
    if (ongoing_navigation->state.has<CanonicalNavigation::Populating>() && ongoing_navigation->host() == this)
        ongoing_navigation->loader()->reclaim_response_body_after_failed_handoff();

    m_history_recorded_url_for_current_load.clear();
    navigable->did_cancel_navigation(move(navigation_id));
}

void WebContentPage::did_change_hosted_navigable_state(Web::HTML::CrossProcessId navigable_id, Web::HTML::HostedNavigableState state)
{
    // Only the process hosting a navigable's active document speaks for its state.
    auto navigable = hosted_navigable(navigable_id);
    if (!navigable.has_value())
        return;

    // A page standing in for a document the navigable lost, as the page displaying the tab after its process crashed
    // does, does not speak for that document.
    if (!hosts_active_document_of(*navigable))
        return;

    navigable->update_hosted_state(move(state));
}

// https://html.spec.whatwg.org/multipage/nav-history-apis.html#dom-opener
// 1. If the given value is null and this's browsing context is non-null, then set this's browsing context's opener
//    browsing context to null.
// NB: Only the process hosting a navigable's active document disowns its browsing context's opener.
void WebContentPage::did_disown_opener_browsing_context(Web::HTML::CrossProcessId navigable_id)
{
    auto navigable = hosted_navigable(navigable_id);
    if (!navigable.has_value() || !hosts_active_document_of(*navigable))
        return;
    navigable->set_opener_browsing_context({});
}

void WebContentPage::did_change_navigable_container_state(Web::HTML::CrossProcessId navigable_id, Web::HTML::ReplicatedContainerState state)
{
    auto navigable = traversable().find(navigable_id);
    if (!navigable.has_value() || !holds_container_of(*navigable, "did_change_navigable_container_state"sv))
        return;
    navigable->update_container_state(move(state));
}

void WebContentPage::did_update_frame_stats_displayed(Web::HTML::CrossProcessId frame_id, bool displayed)
{
    if (!Core::StatsLog::the())
        return;
    auto child_frame = traversable().find(frame_id);
    if (!child_frame.has_value() || !holds_container_of(*child_frame, "did_update_frame_stats_displayed"sv))
        return;
    child_frame->set_stats_displayed(displayed);
}

void WebContentPage::did_update_child_frame_viewport(Web::HTML::CrossProcessId frame_id, Web::DevicePixelRect viewport_rect, Web::DevicePixelRect viewport_intersection, double device_pixel_ratio)
{
    auto child_frame = traversable().find(frame_id);
    if (!child_frame.has_value() || !holds_container_of(*child_frame, "did_update_child_frame_viewport"sv))
        return;
    child_frame->set_viewport(viewport_rect, viewport_intersection, device_pixel_ratio);
}

// The page found the pointer over a child frame another process hosts, and hands the event down to that process. The
// view displaying the tab waits for the event until the process it went to finishes it.
void WebContentPage::did_forward_mouse_event_to_child_frame(Web::HTML::CrossProcessId frame_id, Web::MouseEvent event)
{
    auto child_frame = traversable().find(frame_id);
    if (!child_frame.has_value() || !holds_container_of(*child_frame, "did_forward_mouse_event_to_child_frame"sv) || !child_frame->has_remote_host()) {
        did_finish_handling_input_event(event.id, Web::EventResult::Dropped);
        return;
    }

    auto& host = child_frame->remote_host();
    view().did_forward_input_event(*this, event.id, host);
    host.async_mouse_event_in_hosted_root(frame_id, move(event));
}

// The page found a pinch over a child frame another process hosts, whose document gets the pinch's ctrl+wheel event.
void WebContentPage::did_forward_pinch_event_to_child_frame(Web::HTML::CrossProcessId frame_id, Web::PinchEvent event)
{
    auto child_frame = traversable().find(frame_id);
    if (!child_frame.has_value() || !holds_container_of(*child_frame, "did_forward_pinch_event_to_child_frame"sv) || !child_frame->has_remote_host()) {
        did_finish_handling_input_event(event.id, Web::EventResult::Dropped);
        return;
    }

    auto& host = child_frame->remote_host();
    view().did_forward_pinch_event(*this, event, host);
    // NB: The frame has yet to dispatch the event, whatever the page that forwards it claims.
    event.wheel_event_was_dispatched = false;
    host.async_pinch_event_in_hosted_root(frame_id, event);
}

void WebContentPage::did_destroy_child_frame(Web::HTML::CrossProcessId frame_id)
{
    auto child_frame = traversable().find(frame_id);
    if (!child_frame.has_value() || !holds_container_of(*child_frame, "did_destroy_child_frame"sv))
        return;
    traversable().remove(*child_frame);
}

Messages::WebContentClient::DidStartDownloadWithoutRequestResponse WebContentPage::did_start_download_without_request(URL::URL url, ByteString suggested_filename, Optional<u64> total_size)
{
    auto destination = choose_download_destination_or_report_error(url, suggested_filename);
    if (!destination.has_value())
        return { Optional<u64> {} };

    auto& file_downloader = Application::the().file_downloader();
    auto download_id = file_downloader.start_download(client().is_private(), url, destination.release_value(), total_size);
    if (!is_download_in_progress(file_downloader, download_id))
        return { Optional<u64> {} };

    m_renderer_owned_downloads.set(download_id);

    file_downloader.set_cancel_callback(download_id, [page = NonnullRefPtr<WebContentPage>(*this), download_id] {
        if (!page->is_open())
            return;
        page->m_renderer_owned_downloads.remove(download_id);
        page->async_cancel_download(download_id);
    });

    return { download_id };
}

Messages::WebContentClient::DidStartDownloadResponse WebContentPage::did_start_download(Web::HTML::CrossProcessId navigable_id, Optional<Utf16String> navigation_id, URL::URL url, ByteString suggested_filename, Optional<u64> total_size, int request_server_client_id, u64 request_server_request_id, ByteBuffer initial_data, JsonValue network_response)
{
    // A download taking over an in-flight population's response body ends that navigation without a document;
    // the reporting process ends its side when its population output says the download was handled. The body
    // identifiers preserve the original RequestServer transfer lease, so only the process and navigation that
    // received that response may claim it, and only while its population is in flight.
    FileDownloader::ResponseFinished on_response_finished;
    bool matches_in_flight_navigation = false;
    if (auto navigable = traversable().top_level_traversable().find(navigable_id); navigable.has_value()) {
        auto ongoing_navigation = navigation_id.has_value() ? navigable->navigation_with_id(*navigation_id) : nullptr;
        bool owns_navigation = ongoing_navigation
            && ((ongoing_navigation->state.has<CanonicalNavigation::CreatingNavigationParams>() && ongoing_navigation->worker() == this)
                || (ongoing_navigation->state.has<CanonicalNavigation::Populating>() && ongoing_navigation->host() == this));
        // The active host can report a response after navigation is canceled, so BiDi still needs its terminal error.
        if (!owns_navigation && !traversable().hosts(*navigable, *this))
            return { Optional<u64> {} };

        if (auto* bidi = WebDriverBiDi::the();
            bidi && network_response.is_object()) {
            on_response_finished = [weak_bidi = bidi->make_weak_ptr(),
                                       context = WebDriverBiDi::navigable_id(*navigable),
                                       metadata = network_response.as_object()](
                                       u64 decoded_size,
                                       Requests::RequestTimingInfo const* timing,
                                       Optional<StringView> error) {
                if (weak_bidi)
                    weak_bidi->download_response_finished(context, metadata, decoded_size,
                        timing, error);
            };
        }
        if (owns_navigation && ongoing_navigation->loader()->response_body_matches(request_server_client_id, request_server_request_id)) {
            matches_in_flight_navigation = true;
            navigable->did_cancel_navigation(*navigation_id);
        }
    }
    if (!matches_in_flight_navigation) {
        if (on_response_finished)
            on_response_finished(
                0, nullptr,
                "Navigation response is no longer available for download"sv);
        return { Optional<u64> {} };
    }

    auto destination = choose_download_destination_or_report_error(url, suggested_filename);
    if (!destination.has_value()) {
        if (on_response_finished)
            on_response_finished(0, nullptr,
                "Download destination was not selected"sv);
        return { Optional<u64> {} };
    }

    auto& file_downloader = Application::the().file_downloader();
    auto download_id = file_downloader.adopt_download(client().is_private(), url, destination.release_value(), total_size, request_server_client_id, request_server_request_id, initial_data.bytes(),
        move(on_response_finished));
    if (!is_download_in_progress(file_downloader, download_id))
        return { Optional<u64> {} };

    return { download_id };
}

void WebContentPage::did_receive_download_data(u64 download_id, ByteBuffer data)
{
    if (!m_renderer_owned_downloads.contains(download_id))
        return;

    Application::the().file_downloader().append_download_data(download_id, data.bytes());
}

void WebContentPage::did_finish_download(u64 download_id)
{
    if (!m_renderer_owned_downloads.contains(download_id))
        return;

    m_renderer_owned_downloads.remove(download_id);
    Application::the().file_downloader().finish_download(download_id);
}

void WebContentPage::did_fail_download(u64 download_id, String error)
{
    if (!m_renderer_owned_downloads.contains(download_id))
        return;

    m_renderer_owned_downloads.remove(download_id);
    Application::the().file_downloader().fail_download(download_id, move(error));
}

void WebContentPage::did_finish_loading(Web::HTML::CrossProcessId navigable_id, Web::HTML::CrossProcessId document_id)
{
    // NB: Only the page hosting the active document finishes loading it. A page standing in for a document the
    //     navigable lost, as the page displaying the tab after its process crashed does, finishes loading the stand-in;
    //     that must not surface in the view.
    auto navigable = traversable().find(navigable_id);
    if (!navigable.has_value() || !hosts_active_document_of(*navigable))
        return;

    if (navigable->active_document().id() != document_id)
        return;

    if (navigable->tracked_load_navigation_id() != navigable->active_document_load().navigation_id)
        return;

    navigable->clear_active_document_load();
    if (!navigable->is_top_level_traversable())
        return;

    traversable().presentation().set_loading(false);
    if (hosts_top_level_document()) {
        auto const& committed_url = traversable().active_document_url();

        // The WebUI channel goes to the process the UI process gave the WebUI's document to, never by a URL a process
        // reports. A page that loads anything else loses its channel.
        if (auto const& web_ui_host = navigable->active_document().web_ui_host(); web_ui_host.has_value() && client().hosted_web_ui() == web_ui_host) {
            if (auto web_ui = WebUI::create(client(), m_id, *web_ui_host); web_ui.is_error()) {
                warnln("Could not create WebUI for {}: {}", committed_url, web_ui.error());
                m_web_ui = nullptr;
            } else {
                m_web_ui = web_ui.release_value();
            }
        } else {
            m_web_ui = nullptr;
        }

        auto title = shows_error_page(traversable()) ? Optional<String> {} : history_title(traversable().presentation().title(), committed_url);

        dbgln_if(WEBVIEW_HISTORY_DEBUG, "[History] Load finished for page {} at '{}' with title '{}'",
            m_id,
            committed_url,
            title.has_value() ? title->bytes_as_string_view() : "<none>"sv);

        maybe_record_history_visit_for_current_load(committed_url, title, "load finish"sv);
        if (title.has_value())
            client().session().history_store->update_title(committed_url, *title);
        if (auto const& favicon_hash = traversable().presentation().favicon_hash(); favicon_hash.has_value())
            client().session().history_store->update_favicon(committed_url, *favicon_hash);

        view().did_finish_top_level_load(committed_url);
    }
}

void WebContentPage::did_change_title(Utf16String title)
{
    if (auto process = WebView::Application::process_manager().find_child(client().child_id()); process.has_value())
        process->set_title(title);

    if (hosts_top_level_document()) {
        auto const& url = traversable().active_document_url();
        if (!title.is_empty() && !shows_error_page(traversable())) {
            auto title_utf8 = title.to_utf8();

            maybe_record_history_visit_for_current_load(url, title_utf8, "title change"sv);
            dbgln_if(WEBVIEW_HISTORY_DEBUG, "[History] Title changed for page {} at '{}' to '{}'",
                m_id,
                url,
                title_utf8);

            client().session().history_store->update_title(url, title_utf8);
        }

        if (title.is_empty())
            title = Utf16String::from_utf8(url.serialize());
    }
    if (hosts_active_document_of(traversable())) {
        if (title.is_empty())
            title = Utf16String::from_utf8(traversable().active_document_url().serialize());
        traversable().presentation().set_title(move(title));
    }
}

void WebContentPage::did_request_context_menu(Web::HTML::CrossProcessId local_root_id, Gfx::IntPoint content_position, Web::ContextMenuForInputEventsTarget for_input_events_target, Optional<Utf16String> misspelled_word, Optional<u64> text_fragment_generation_id)
{
    if (auto position = tab_position(local_root_id, content_position); position.has_value())
        view().did_request_page_context_menu(*this, *position, for_input_events_target, move(misspelled_word), text_fragment_generation_id);
}

void WebContentPage::did_request_spelling_menu_action_for_testing(Web::SpellingMenuActionForTesting action, u32 guess_index)
{
    // NB: This activates the tab's own menu actions, so only automation may ask for it.
    if (!WebContentClient::renderers_may_synthesize_input()) {
        client().did_misbehave("did_request_spelling_menu_action_for_testing"sv, "not driven by WebDriver or a test"sv);
        return;
    }
    view().activate_spelling_menu_action_for_testing(*this, action, guess_index);
}

void WebContentPage::did_request_page_translation_for_testing(String target_language)
{
    // NB: This translates the tab as its menu would, so only automation may ask for it.
    if (!WebContentClient::renderers_may_synthesize_input()) {
        client().did_misbehave("did_request_page_translation_for_testing"sv, "not driven by WebDriver or a test"sv);
        return;
    }
    view().page_translation().translate_for_testing({}, move(target_language));
}

void WebContentPage::did_request_link_context_menu(Web::HTML::CrossProcessId local_root_id, Gfx::IntPoint content_position, Web::HTML::PreparedNavigationDescriptor navigation, ByteString, unsigned, Optional<u64> text_fragment_generation_id)
{
    auto verified_navigation = navigation_from_page(client(), move(navigation));
    if (!verified_navigation.has_value())
        return;
    if (auto position = tab_position(local_root_id, content_position); position.has_value())
        view().did_request_link_context_menu(*position, verified_navigation.release_value(), text_fragment_generation_id);
}

void WebContentPage::did_generate_text_fragment_url(u64 request_id, Optional<URL::URL> url)
{
    if (auto reply = take_reply_from_renderer<PendingReply<Optional<Optional<URL::URL>>>>(request_id))
        reply->complete(move(url));
}

void WebContentPage::did_request_image_context_menu(Web::HTML::CrossProcessId local_root_id, Gfx::IntPoint content_position, Web::HTML::PreparedNavigationDescriptor navigation, ByteString, unsigned, Optional<Gfx::ShareableBitmap> bitmap)
{
    auto verified_navigation = navigation_from_page(client(), move(navigation));
    if (!verified_navigation.has_value())
        return;
    if (auto position = tab_position(local_root_id, content_position); position.has_value())
        view().did_request_image_context_menu(*position, verified_navigation.release_value(), move(bitmap));
}

void WebContentPage::did_request_media_context_menu(Web::HTML::CrossProcessId local_root_id, Gfx::IntPoint content_position, ByteString, unsigned, Web::MediaContextMenu menu, Optional<Web::HTML::PreparedNavigationDescriptor> navigation)
{
    Optional<Web::HTML::PreparedNavigationDescriptor> verified_navigation;
    if (navigation.has_value()) {
        verified_navigation = navigation_from_page(client(), navigation.release_value());
        if (!verified_navigation.has_value())
            return;
    }
    if (auto position = tab_position(local_root_id, content_position); position.has_value())
        view().did_request_media_context_menu(*this, *position, move(menu), move(verified_navigation));
}

void WebContentPage::did_get_highlighted_source(u64 request_id, String html)
{
    if (auto reply = take_reply_from_renderer<PendingReply<Optional<String>>>(request_id))
        reply->complete(move(html));
}

void WebContentPage::did_get_debugger_environments(u64 request_id, Optional<String> error, Vector<DebuggerEnvironment> environments)
{
    auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnDebuggerEnvironmentsReceived>>(request_id);
    if (!reply)
        return;
    if (error.has_value())
        reply->complete(Error::from_string_view(error->bytes_as_string_view()));
    else
        reply->complete(move(environments));
}

void WebContentPage::did_evaluate_javascript_in_debugger_frame(u64 request_id, Optional<String> error, DebuggerEvaluationResult result)
{
    auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnDebuggerEvaluationComplete>>(request_id);
    if (!reply)
        return;
    if (error.has_value())
        reply->complete(error.release_value());
    else
        reply->complete(move(result));
}

void WebContentPage::did_get_debugger_object_properties(u64 request_id, Optional<String> error, DebuggerObjectProperties properties)
{
    auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnDebuggerObjectPropertiesReceived>>(request_id);
    if (!reply)
        return;
    if (error.has_value())
        reply->complete(error.release_value());
    else
        reply->complete(move(properties));
}

void WebContentPage::did_get_debugger_source_positions(u64 request_id, Vector<DebuggerSourcePosition> positions)
{
    if (auto reply = take_reply_from_renderer<ReplyFor<DevTools::DevToolsDelegate::OnDebuggerSourcePositionsReceived>>(request_id))
        reply->complete(move(positions));
}

void WebContentPage::did_request_alert(Utf16String message)
{
    if (auto reply = open_dialog(Web::PendingDialog::Alert, message))
        view().request_alert(message, reply.release_nonnull());
}

void WebContentPage::did_request_before_unload(String source)
{
    if (auto reply = open_dialog(Web::PendingDialog::BeforeUnload, {}))
        view().request_before_unload(source, reply.release_nonnull());
}

void WebContentPage::did_request_confirm(Utf16String message)
{
    if (auto reply = open_dialog(Web::PendingDialog::Confirm, message))
        view().request_confirm(message, reply.release_nonnull());
}

void WebContentPage::did_request_prompt(Utf16String message, Utf16String default_)
{
    if (auto reply = open_dialog(Web::PendingDialog::Prompt, message))
        view().request_prompt(message, default_, reply.release_nonnull());
}

void WebContentPage::did_change_favicon(Gfx::ShareableBitmap favicon)
{
    if (!favicon.is_valid()) {
        dbgln("DidChangeFavicon: Received invalid favicon");
        return;
    }

    if (hosts_top_level_document()) {
        auto const& url = traversable().active_document_url();
        maybe_record_history_visit_for_current_load(url, history_title(traversable().presentation().title(), url), "favicon change"sv);
    }
    if (hosts_active_document_of(traversable()))
        traversable().presentation().set_favicon(*favicon.bitmap());
}

void WebContentPage::did_request_delete_all_cookies(u64 request_id, URL::URL url)
{
    if (!WebContentClient::renderers_may_access_cookies_like_http()) {
        client().did_misbehave("did_request_delete_all_cookies"sv, "not driven by WebDriver"sv);
        return;
    }

    client().session().cookie_jar->delete_all_cookies(url);
    async_did_delete_all_cookies(request_id);
}

Messages::WebContentClient::DidRequestNewWebViewResponse WebContentPage::did_request_new_web_view(Web::HTML::ActivateTab activate_tab, Web::HTML::WebViewHints hints, Web::HTML::CrossProcessId current_navigable_id, Utf16String name, bool no_opener, Optional<URL::URL> current_document_base_url)
{
    // The requesting navigable is one the requesting page hosts. A request naming one it does not is refused before the
    // chrome is asked for a view, so nothing is created for a traversable that never will be.
    auto current_navigable = hosted_navigable(current_navigable_id);
    if (!current_navigable.has_value())
        return { {}, {}, {}, {}, {}, {}, {}, Web::HTML::VisibilityState::Hidden, {} };

    Optional<CanonicalNavigable&> picture_in_picture_navigable;
    if (hints.picture_in_picture_window.has_value()) {
        picture_in_picture_navigable = hosted_navigable(hints.picture_in_picture_window->navigable_id);
        if (!picture_in_picture_navigable.has_value())
            return { {}, {}, {}, {}, {}, {}, {}, Web::HTML::VisibilityState::Hidden, {} };

        // https://w3c.github.io/picture-in-picture/#request-picture-in-picture
        // A window needs and consumes the activation of the user's input, which the renderer checks, unless the document
        // asking for it has one open. A compromised renderer skips that check, so whatever the popup setting, the UI
        // process checks the activation of input it delivered, and the window it opened for the navigable's document.
        if (!PictureInPictureManager::the().document_has_open_window(*picture_in_picture_navigable) && !consume_transient_user_activation())
            return { {}, {}, {}, {}, {}, {}, {}, Web::HTML::VisibilityState::Hidden, {} };
    } else {
        // https://html.spec.whatwg.org/multipage/document-sequences.html#the-rules-for-choosing-a-navigable
        // --> If currentNavigable's active window does not have transient activation and the user agent has been
        //     configured to not show popups
        // NB: The popup blocker runs in the renderer, which a compromised renderer skips, so the UI process checks the
        //     activation of the input it delivered to the page.
        if (Application::the().blocks_pop_ups() && !has_transient_user_activation())
            return { {}, {}, {}, {}, {}, {}, {}, Web::HTML::VisibilityState::Hidden, {} };
    }

    auto root_navigable_id = Application::the().allocate_ui_process_cross_process_id();
    auto document_id = Application::the().allocate_ui_process_cross_process_id();

    // A Picture-in-Picture window is not chosen by web content, so it skips the rules for choosing a navigable.
    auto [traversable, window_type] = picture_in_picture_navigable.has_value()
        ? CanonicalNavigable::ChosenTopLevelTraversable { CanonicalTraversable::create_a_new_top_level_traversable(client().session(), root_navigable_id, {}, Web::HTML::create_initial_session_history_entry_descriptor(Application::the().allocate_ui_process_cross_process_id(), {}, {}), document_id), Web::HTML::WindowType::NewAndUnrestricted }
        : current_navigable->choose_a_new_top_level_traversable(*this, name, no_opener, move(current_document_base_url), root_navigable_id, document_id);
    if (picture_in_picture_navigable.has_value())
        traversable.set_owner_tab_id(this->traversable().tab_id());

    auto new_page_id = Application::the().allocate_page_id();
    client().open_page_for_new_top_level_traversable(new_page_id, traversable);

    String window_handle;
    if (picture_in_picture_navigable.has_value()) {
        window_handle = PictureInPictureManager::the().open_window(*this, *picture_in_picture_navigable, traversable, hints.picture_in_picture_window->video_size);
    } else if (view().on_new_web_view) {
        window_handle = view().on_new_web_view(activate_tab, hints, traversable);
    }

    if (!traversable.view().surface()) {
        client().discard_page_of_undisplayed_top_level_traversable(new_page_id);
        traversable.destroy();
        return { {}, {}, {}, {}, {}, {}, {}, Web::HTML::VisibilityState::Hidden, move(window_handle) };
    }
    traversable.represent_group_everywhere();

    auto environment_id = traversable.active_document().relevant_global_object().relevant_settings_object().id();
    Optional<u64> browsing_context_group_id = traversable.active_browsing_context().group()->id();
    return { new_page_id, root_navigable_id, traversable.active_session_history_entry()->descriptor(), move(environment_id), document_id, browsing_context_group_id, window_type, traversable.system_visibility_state(), move(window_handle) };
}

void WebContentPage::did_close_browsing_context()
{
    traversable().remove_page(*this);
    // NB: Before unregistering, so an acknowledged embedded discard closes an otherwise-unused server immediately.
    m_detached_close_pending = false;
    close();
    client().unregister_embedded_page(m_id);

    if (m_traversable && hosts_top_level_document()) {
        // NB: The view destroys the traversable once no surface shows the tab.
        auto& view = m_traversable->view();
        m_traversable = nullptr;
        view.did_close_browsing_context();
    }
}

void WebContentPage::did_complete_close_preflight(u64 request_id, bool approved)
{
    if (auto reply = take_reply_from_renderer<ClosePreflightReply>(request_id))
        reply->complete(approved);
}

void WebContentPage::did_request_select_dropdown(Web::HTML::CrossProcessId local_root_id, Gfx::IntPoint content_position, i32 minimum_width, Vector<Web::HTML::SelectItem> items)
{
    auto reply = SelectDropdownReply::create([page = NonnullRefPtr { *this }](Optional<u32> item) {
        page->async_select_dropdown_closed(item);
    },
        {});
    auto local_root = hosted_navigable(local_root_id);
    if (!local_root.has_value()) {
        reply->cancel();
        return;
    }
    content_position.translate_by(traversable().local_root_offset(*local_root).to_type<int>());
    if (auto* view = expect_reply_from_ui(reply))
        view->request_select_dropdown(content_position, minimum_width, move(items), move(reply));
}

void WebContentPage::did_request_primary_paste()
{
    if (!has_transient_user_activation())
        return;
    auto text = Application::the().clipboard_text(Application::ClipboardType::Selection);
    async_paste(text);
}

void WebContentPage::did_update_primary_selection(String text)
{
    Application::the().set_clipboard_text(move(text), Application::ClipboardType::Selection);
}

void WebContentPage::did_update_session_history_entry_scroll_restoration_mode(Web::HTML::CrossProcessId navigable_id, Web::HTML::SessionHistoryEntryIdentity entry_identity, Web::HTML::ScrollRestorationMode scroll_restoration_mode)
{
    auto navigable = hosted_navigable(navigable_id);
    if (!navigable.has_value())
        return;
    if (scroll_restoration_mode != Web::HTML::ScrollRestorationMode::Auto
        && scroll_restoration_mode != Web::HTML::ScrollRestorationMode::Manual) {
        client().did_misbehave("did_update_session_history_entry_scroll_restoration_mode"sv, "invalid scroll restoration mode"sv);
        return;
    }
    navigable->top_level_traversable().update_session_history_entry_scroll_restoration_mode(*navigable, entry_identity, scroll_restoration_mode);
}

void WebContentPage::did_request_pinch_event_for_testing(Web::HTML::CrossProcessId local_root_id, Web::PinchEvent event)
{
    // NB: As for a key event for testing, the event is dispatched as the user's own.
    if (!WebContentClient::renderers_may_synthesize_input()) {
        client().did_misbehave("did_request_pinch_event_for_testing"sv, "not driven by WebDriver or a test"sv);
        return;
    }

    // The event is relative to the viewport of the local root its page dispatches input to.
    if (auto root = traversable().find(local_root_id); root.has_value())
        event.position.translate_by(traversable().local_root_offset(*root));
    // Only the compositor sets this, on its way to WebContent.
    event.async_zoom_performed_default_action = false;
    view().enqueue_pinch_event_for_testing(event);
}

void WebContentPage::did_request_webdriver_mouse_event(u64 request_id, Web::HTML::CrossProcessId local_root_id, Web::MouseEvent event)
{
    // NB: As for a key event for testing, the event is dispatched as the user's own.
    if (!WebContentClient::renderers_may_synthesize_input()) {
        client().did_misbehave("did_request_webdriver_mouse_event"sv, "not driven by WebDriver or a test"sv);
        return;
    }
    auto on_handled = [page = NonnullRefPtr<WebContentPage>(*this), request_id]() {
        page->async_did_handle_webdriver_mouse_event(request_id);
    };

    // The event is relative to the viewport of the local root its page dispatches input to.
    if (auto root = traversable().find(local_root_id); root.has_value()) {
        auto offset = traversable().local_root_offset(*root);
        event.position.translate_by(offset);
        event.screen_position.translate_by(offset);
    }
    // Only the compositor sets these, on its way to WebContent.
    event.async_scroll_performed_default_action = false;
    event.scrollbar_dragged_by_compositor = {};
    event.wheel_gesture_mark = {};
    view().enqueue_webdriver_mouse_event(move(event), move(on_handled));
}

void WebContentPage::request_unload_check(Web::HTML::CrossProcessId navigable_id, Web::HTML::CrossProcessId check_id)
{
    auto navigable = hosted_navigable(navigable_id);
    if (!navigable.has_value()) {
        async_unload_check_result(check_id, Web::HTML::CheckIfUnloadingIsCanceledResult::Continue);
        return;
    }
    Vector<Web::HTML::CrossProcessId> inclusive_descendants;
    navigable->for_each_in_inclusive_subtree([&](CanonicalNavigable const& descendant) {
        inclusive_descendants.append(descendant.id());
        return IterationDecision::Continue;
    });
    (void)navigable->top_level_traversable().check_if_unloading_is_canceled(move(inclusive_descendants), {}, {}, {}, Web::HTML::UnloadPromptShown::No,
        [page = NonnullRefPtr<WebContentPage>(*this), check_id](Web::HTML::CheckIfUnloadingIsCanceledResult result, Web::HTML::UnloadPromptShown) {
            page->async_unload_check_result(check_id, result);
        });
}

Messages::WebContentClient::StartWorkerAgentResponse WebContentPage::start_worker_agent(Web::HTML::WorkerAgentStartRequest request)
{
    // A page hosting an isolated iframe's document belongs to its tab's view as much as the view's own page does.
    auto outside_settings = hosted_environment(request.outside_settings.id);
    auto agent_id = WorkerProcessManager::the().start_worker_agent(client(), m_id, outside_settings, move(request));
    return { agent_id };
}

Messages::WebContentClient::DidRequestStorageUsageResponse WebContentPage::did_request_storage_usage(Web::HTML::EnvironmentId environment_id)
{
    auto canonical_key = canonical_storage_key(environment_id);
    if (!canonical_key.has_value())
        return 0;
    return client().session().storage_jar->usage(*canonical_key);
}

void WebContentPage::did_post_broadcast_channel_message(Web::HTML::PostedBroadcastChannelMessage message)
{
    if (auto settings = hosted_environment(message.environment_id); settings.has_value())
        WorkerProcessManager::the().post_broadcast_channel_message(move(message), *settings, client().pid(), client().is_private());
}

void WebContentPage::close_worker_agent(Web::HTML::WorkerAgentId agent_id, Web::HTML::WorkerAgentOwnerToken owner_token)
{
    WorkerProcessManager::the().close_worker_agent(client(), agent_id, owner_token);
}

void WebContentPage::rtc_transform_init(Web::HTML::WorkerAgentId agent_id, u64 transform_id, Web::HTML::SerializedTransferRecord options_record)
{
    WorkerProcessManager::the().rtc_transform_init(client(), agent_id, transform_id, move(options_record));
}

void WebContentPage::rtc_transform_teardown(Web::HTML::WorkerAgentId agent_id, u64 transform_id)
{
    WorkerProcessManager::the().rtc_transform_teardown(client(), agent_id, transform_id);
}

void WebContentPage::rtc_transform_encoded_audio_frame(Web::HTML::WorkerAgentId agent_id, u64 transform_id, ByteBuffer payload, u32 ssrc, u8 payload_type, u32 rtp_timestamp, u16 sequence_number)
{
    WorkerProcessManager::the().rtc_transform_encoded_audio_frame(client(), agent_id, transform_id, move(payload), ssrc, payload_type, rtp_timestamp, sequence_number);
}

void WebContentPage::did_finish_test(String text)
{
    if (view().on_test_finish)
        view().on_test_finish(text);
}

void WebContentPage::did_set_test_timeout(double milliseconds)
{
    if (view().on_set_test_timeout)
        view().on_set_test_timeout(milliseconds);
}

void WebContentPage::did_receive_reference_test_metadata(JsonValue metadata)
{
    if (view().on_reference_test_metadata)
        view().on_reference_test_metadata(metadata);
}

void WebContentPage::did_simulate_worker_request_server_connection_loss()
{
    VERIFY(Application::web_content_options().is_test_mode == IsTestMode::Yes);
    if (auto result = WorkerProcessManager::the().simulate_request_server_connection_loss_for_testing(client(), m_id); result.is_error()) {
        warnln("Unable to reconnect WebWorker processes to RequestServer: {}", result.error());
        VERIFY_NOT_REACHED();
    }
}

Messages::WebContentTestClient::DidRequestUiProcessSessionHistoryForTestingResponse WebContentPage::did_request_ui_process_session_history_for_testing()
{
    return { answer_from_view<String>("{}"_string, [](auto& view) { return view.ui_process_session_history_for_testing(); }) };
}

Messages::WebContentTestClient::DidRequestSiteIsolationProcessTreeForTestingResponse WebContentPage::did_request_site_isolation_process_tree_for_testing()
{
    return { dump_process_tree() };
}

Messages::WebContentTestClient::DidRequestTabIsPlayingAudioForTestingResponse WebContentPage::did_request_tab_is_playing_audio_for_testing()
{
    return view().tab_indicator_visible(TabIndicator::PlayingAudio);
}

Messages::WebContentTestClient::DidRequestTabIsHeardPlayingAudioForTestingResponse WebContentPage::did_request_tab_is_heard_playing_audio_for_testing()
{
    return view().is_playing_audio();
}

Messages::WebContentTestClient::DidRequestTabAudibilityChangeCountForTestingResponse WebContentPage::did_request_tab_audibility_change_count_for_testing()
{
    return view().playing_audio_indicator_change_count();
}

void WebContentPage::did_request_crash_of_remote_frame_processes_for_testing()
{
    traversable().for_each_in_subtree([](CanonicalNavigable& child_frame) {
        if (child_frame.has_remote_host())
            child_frame.remote_host().async_debug_request("crash-current-page"sv, ""sv);
        return IterationDecision::Continue;
    });
}

// The tab's Stop and Reload buttons.
void WebContentPage::did_request_stop_loading_for_testing()
{
    view().stop_loading();
}

void WebContentPage::did_request_reload_for_testing(bool bypass_cache)
{
    view().reload(bypass_cache ? BypassCache::Yes : BypassCache::No);
}

// The tab's Back and Forward buttons, which traverse the history with no source document.
void WebContentPage::did_request_traverse_history_by_delta_for_testing(i32 delta)
{
    view().traverse_the_history_by_delta(delta);
}

void WebContentPage::reset_state_for_testing()
{
    auto* client = routed_connection();
    if (!client || !client->test_connection())
        return;
    client->transport().flush();
    client->test_connection()->async_reset_state_for_testing(m_id);
}

void WebContentPage::did_reset_state_for_testing(Web::HTML::SessionHistoryEntryDescriptor active_entry)
{
    view().did_reset_state_for_testing(move(active_entry));
}

void WebContentPage::reset_gamepad_access_for_testing()
{
    GamepadManager::the().reset_access_for_testing(*this);
}

Messages::WebContentTestClient::DidRequestCaptureSessionHistorySnapshotForTestingResponse WebContentPage::did_request_capture_session_history_snapshot_for_testing()
{
    return { answer_from_view<bool>(false, [](auto& view) { return view.capture_session_history_snapshot_for_testing(); }) };
}

Messages::WebContentTestClient::DidRequestRestoreSessionHistorySnapshotForTestingResponse WebContentPage::did_request_restore_session_history_snapshot_for_testing()
{
    return { answer_from_view<bool>(false, [](auto& view) { return view.restore_captured_session_history_snapshot_for_testing(); }) };
}

Messages::WebContentTestClient::DidRequestRegisterSessionStoreTabForTestingResponse WebContentPage::did_request_register_session_store_tab_for_testing()
{
    return { answer_from_view<bool>(false, [](auto& view) { return view.register_session_store_tab_for_testing(); }) };
}

// A document populated for a navigable is one the browser process created for it, and has not made active yet.
Messages::WebContentTestClient::DidRequestHasPopulatedDocumentForTestingResponse WebContentPage::did_request_has_populated_document_for_testing(Web::HTML::CrossProcessId navigable_id) const
{
    auto navigable = traversable().top_level_traversable().find(navigable_id);
    if (!navigable.has_value())
        return false;
    bool has_populated_document = false;
    navigable->for_each_populated_document_state([&](auto const&) {
        has_populated_document = true;
    });
    return has_populated_document;
}

Messages::WebContentTestClient::DidRequestSessionStoreTabStateForTestingResponse WebContentPage::did_request_session_store_tab_state_for_testing()
{
    return { answer_from_view<String>("{}"_string, [](auto& view) { return view.session_store_tab_state_for_testing(); }) };
}

// https://w3c.github.io/FileAPI/#add-an-entry
Messages::WebContentClient::DidAddBlobUrlEntryResponse WebContentPage::did_add_blob_url_entry(Web::HTML::EnvironmentId environment_id, Utf16String url, Web::FileAPI::SerializedBlobURLEntry entry)
{
    // 3. Let entry be a new blob URL entry consisting of object and the current settings object.
    auto environment = hosted_environment(environment_id);
    if (!environment.has_value())
        return URL::BlobURLEntry::Token { 0 };
    entry.origin = environment->origin();

    // 4. Set store[url] to entry.
    // NB: A document created from the URL in another process reads local files as local file content does, so the
    //     store remembers whether local file content added the entry.
    auto added_by_local_file_content = client().may_read_local_files() ? AddedByLocalFileContent::Yes : AddedByLocalFileContent::No;
    return client().session().blob_url_store->add_entry(move(url), move(entry), WeakPtr<WebContentClient> { client() }, added_by_local_file_content);
}

// https://w3c.github.io/FileAPI/#dfn-revokeObjectURL
void WebContentPage::did_remove_blob_url_entries(Web::HTML::EnvironmentId environment_id, Vector<Utf16String> urls)
{
    auto environment = hosted_environment(environment_id);
    if (!environment.has_value())
        return;
    client().session().blob_url_store->remove_entries(urls, environment->origin(), WeakPtr<WebContentClient> { client() });
}

Optional<bool> WebContentPage::hosted_environment_has_cross_site_ancestor(Web::HTML::EnvironmentId const& environment_id) const
{
    auto is_of_environment = [&](CanonicalDocument const& document) {
        return document.relevant_global_object().relevant_settings_object().id() == environment_id;
    };

    Optional<bool> result;
    traversable().for_each_in_inclusive_subtree([&](CanonicalNavigable const& navigable) {
        if (traversable().hosts(navigable, *this) && is_of_environment(navigable.active_document())) {
            result = navigable.active_document_has_cross_site_ancestor();
            return IterationDecision::Break;
        }

        // NB: A document being populated has a cross-site ancestor if its navigable's parent's active document has one,
        //     or is of another site.
        navigable.for_each_populated_document_state([&](CanonicalDocumentState const& document_state) {
            auto const& document = *document_state.populated_document->document;
            if (result.has_value() || document.host() != this || !is_of_environment(document))
                return;
            auto const* parent = navigable.parent();
            result = parent && (parent->active_document_has_cross_site_ancestor() || !parent->active_document().origin().is_same_site(document.origin()));
        });
        return result.has_value() ? IterationDecision::Break : IterationDecision::Continue;
    });
    return result;
}

// Script reaches cookies through an environment the page hosts, and only those of that environment's origin. This is
// the environment's cookie partition context, or nothing if the page hosts no such environment, or if the environment's
// top-level document has an opaque origin and so is a third-party context of no site, which has no cookies.
static Optional<HTTP::Cookie::PartitionContext> cookie_partition_context_for(WebContentPage const& page, Optional<Web::HTML::EnvironmentId> const& environment_id, URL::URL const& url)
{
    if (!environment_id.has_value())
        return {};

    auto environment = page.hosted_environment(*environment_id);
    if (!environment.has_value() || !environment->may_use_cookies_of(url))
        return {};

    auto top_level_site = network_isolation_top_level_site(*environment);
    if (!top_level_site.has_value())
        return {};

    return HTTP::Cookie::PartitionContext {
        .top_level_site = top_level_site.release_value(),
        .has_cross_site_ancestor = page.hosted_environment_has_cross_site_ancestor(*environment_id).value_or(true),
    };
}

void WebContentPage::did_set_cookie(Optional<Web::HTML::EnvironmentId> environment_id, URL::URL url, HTTP::Cookie::ParsedCookie cookie, HTTP::Cookie::Source source)
{
    // NB: Only WebDriver sets cookies like HTTP does, for the top-level documents it drives.
    if (source == HTTP::Cookie::Source::Http) {
        if (!WebContentClient::renderers_may_access_cookies_like_http()) {
            client().did_misbehave("did_set_cookie"sv, "HTTP cookie source"sv);
            return;
        }
        client().session().cookie_jar->set_cookie(url, cookie, source, {});
        return;
    }

    auto partition_context = cookie_partition_context_for(*this, environment_id, url);
    if (!partition_context.has_value())
        return;

    client().session().cookie_jar->set_cookie(url, cookie, source, partition_context);
}

Messages::WebContentClient::DidRequestAllCookiesCookiestoreResponse WebContentPage::did_request_all_cookies_cookiestore(Web::HTML::EnvironmentId const& environment_id, URL::URL url)
{
    auto partition_context = cookie_partition_context_for(*this, environment_id, url);
    if (!partition_context.has_value())
        return Vector<HTTP::Cookie::Cookie> {};

    return client().session().cookie_jar->get_all_cookies_cookiestore(url, partition_context);
}

Messages::WebContentClient::DidRequestCookieResponse WebContentPage::did_request_cookie(Optional<Web::HTML::EnvironmentId> const& environment_id, URL::URL url, HTTP::Cookie::Source source)
{
    // NB: Only WebDriver reads cookies like HTTP does, for the top-level documents it drives.
    Optional<HTTP::Cookie::PartitionContext> partition_context;
    if (source == HTTP::Cookie::Source::NonHttp) {
        partition_context = cookie_partition_context_for(*this, environment_id, url);
        if (!partition_context.has_value())
            return HTTP::Cookie::VersionedCookie {};
    }

    HTTP::Cookie::VersionedCookie cookie;
    cookie.cookie = client().session().cookie_jar->get_cookie(url, source, partition_context);
    if (source == HTTP::Cookie::Source::NonHttp)
        cookie.cookie_version = traversable().document_cookie_version(url);
    return cookie;
}

}
