/*
 * Copyright (c) 2026, Ladybird contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Function.h>
#include <AK/QuickSort.h>
#include <AK/StringBuilder.h>
#include <LibWebView/Application.h>
#include <LibWebView/SiteIsolation.h>
#include <LibWebView/SiteIsolationManager.h>
#include <LibWebView/ViewImplementation.h>
#include <LibWebView/WebContentClient.h>

namespace WebView {

static constexpr bool IFRAME_SITE_ISOLATION_DEBUG = false;

SiteIsolationManager& SiteIsolationManager::the()
{
    static auto& manager = *new SiteIsolationManager;
    return manager;
}

StringView SiteIsolationManager::child_frame_owner_name(ChildFrameOwner owner)
{
    switch (owner) {
    case ChildFrameOwner::Local:
        return "local"sv;
    case ChildFrameOwner::Remote:
        return "remote"sv;
    }
    VERIFY_NOT_REACHED();
}

Web::NavigationProcessDecision SiteIsolationManager::decide_navigation_process(
    WebContentClient& parent_client,
    u64 page_id,
    Optional<String> frame_id,
    URL::URL current_url,
    URL::URL target_url,
    Web::NavigationTarget target)
{
    auto web_view_target = target == Web::NavigationTarget::IFrame
        ? NavigationTarget::IFrame
        : NavigationTarget::TopLevel;

    if (target == Web::NavigationTarget::IFrame && frame_id.has_value()) {
        if (auto child_frame = this->child_frame(page_id, *frame_id); child_frame.has_value())
            current_url = embedding_page_url_for_child_frame_navigation(parent_client, page_id, *child_frame, current_url);
    }

    auto decision = WebView::is_url_suitable_for_same_process_navigation(current_url, target_url, web_view_target)
        ? Web::NavigationProcessDecision::Local
        : Web::NavigationProcessDecision::Remote;

    if (target == Web::NavigationTarget::IFrame && frame_id.has_value()) {
        auto target_owner = decision == Web::NavigationProcessDecision::Local
            ? ChildFrameOwner::Local
            : ChildFrameOwner::Remote;
        record_pending_child_frame_navigation(page_id, *frame_id, target_url, target_owner);

        if (decision == Web::NavigationProcessDecision::Local)
            transition_child_frame_to_local(parent_client, page_id, *frame_id);
    }

    return decision;
}

void SiteIsolationManager::did_create_child_frame(u64 page_id, String parent_frame_id, String frame_id)
{
    auto& child_frames = m_child_frames.ensure(page_id, [] {
        return HashMap<String, ChildFrameHost> {};
    });
    child_frames.set(move(frame_id), ChildFrameHost {
        .parent_frame_id = move(parent_frame_id),
    });
}

void SiteIsolationManager::did_update_child_frame_viewport(u64 page_id, String frame_id, Web::DevicePixelRect viewport_rect, double device_pixel_ratio)
{
    auto child_frame = this->child_frame(page_id, frame_id);
    if (!child_frame.has_value())
        return;

    child_frame->viewport_rect = viewport_rect;
    child_frame->device_pixel_ratio = device_pixel_ratio;
    if (child_frame->is_remote()) {
        child_frame->remote_client->async_set_viewport(
            child_frame->remote_page_id,
            viewport_rect.size(),
            device_pixel_ratio,
            Web::ViewportIsFullscreen::No);
    }
}

void SiteIsolationManager::did_destroy_child_frame(WebContentClient& parent_client, u64 page_id, StringView frame_id)
{
    auto child_frames = m_child_frames.get(page_id);
    if (!child_frames.has_value())
        return;

    transition_child_frame_to_local(parent_client, page_id, frame_id);

    child_frames->remove(frame_id);
    if (child_frames->is_empty())
        m_child_frames.remove(page_id);

    if (auto focused_frame_id = m_focused_child_frames.get(page_id); focused_frame_id.has_value() && *focused_frame_id == frame_id)
        m_focused_child_frames.remove(page_id);
}

Optional<SiteIsolationManager::ChildFrameHost&> SiteIsolationManager::child_frame(u64 page_id, StringView frame_id)
{
    auto child_frames = m_child_frames.get(page_id);
    if (!child_frames.has_value())
        return {};

    return child_frames->get(frame_id);
}

Optional<SiteIsolationManager::ChildFrameHost const&> SiteIsolationManager::child_frame(u64 page_id, StringView frame_id) const
{
    auto child_frames = m_child_frames.get(page_id);
    if (!child_frames.has_value())
        return {};

    return child_frames->get(frame_id);
}

bool SiteIsolationManager::client_owns_page(WebContentClient const& client, u64 page_id)
{
    return client.m_views.contains(page_id) || client.m_embedded_pages.contains(page_id);
}

Optional<SiteIsolationManager::ParentFrame> SiteIsolationManager::parent_frame_for_remote_page(WebContentClient& remote_client, u64 remote_page_id)
{
    Optional<ParentFrame> result;

    WebContentClient::for_each_client([&](auto& parent_client) {
        for (auto& page_child_frames : m_child_frames) {
            auto parent_page_id = page_child_frames.key;
            if (!client_owns_page(parent_client, parent_page_id))
                continue;

            for (auto& child_frame_entry : page_child_frames.value) {
                auto& child_frame = child_frame_entry.value;
                if (!child_frame.is_remote() || child_frame.remote_client.ptr() != &remote_client || child_frame.remote_page_id != remote_page_id)
                    continue;

                result = ParentFrame {
                    .parent_client = &parent_client,
                    .page_id = parent_page_id,
                    .frame_id = child_frame_entry.key,
                    .child_frame = &child_frame,
                };
                return IterationDecision::Break;
            }
        }

        return IterationDecision::Continue;
    });

    return result;
}

Optional<URL::URL> SiteIsolationManager::document_url_for_child_frame(ChildFrameHost const& child_frame)
{
    if (child_frame.last_committed_url.has_value())
        return child_frame.last_committed_url;

    if (child_frame.pending_navigation.has_value())
        return child_frame.pending_navigation->target_url;

    return {};
}

URL::URL SiteIsolationManager::document_url_for_page(WebContentClient& client, u64 page_id, URL::URL const& fallback_url)
{
    if (auto view = client.view_for_page_id(page_id); view.has_value())
        return view->url();

    if (client.m_embedded_pages.contains(page_id)) {
        if (auto parent_frame = parent_frame_for_remote_page(client, page_id); parent_frame.has_value()) {
            if (auto url = document_url_for_child_frame(*parent_frame->child_frame); url.has_value())
                return *url;
        }
    }

    return fallback_url;
}

URL::URL SiteIsolationManager::embedding_page_url_for_child_frame_navigation(WebContentClient& parent_client, u64 page_id, ChildFrameHost const& child_frame, URL::URL const& fallback_url)
{
    if (auto parent_frame = this->child_frame(page_id, child_frame.parent_frame_id); parent_frame.has_value()) {
        if (auto url = document_url_for_child_frame(*parent_frame); url.has_value())
            return *url;
    }

    return document_url_for_page(parent_client, page_id, fallback_url);
}

SiteIsolationManager::ChildFrameOwner SiteIsolationManager::expected_child_frame_owner_for_navigation(WebContentClient& parent_client, u64 page_id, ChildFrameHost const& child_frame, URL::URL const& url)
{
    auto embedding_page_url = embedding_page_url_for_child_frame_navigation(parent_client, page_id, child_frame, url);
    if (WebView::is_url_suitable_for_same_process_navigation(embedding_page_url, url, NavigationTarget::IFrame))
        return ChildFrameOwner::Local;
    return ChildFrameOwner::Remote;
}

bool SiteIsolationManager::is_child_frame_commit_allowed(WebContentClient& parent_client, u64 page_id, StringView frame_id, ChildFrameHost const& child_frame, URL::URL const& url, ChildFrameOwner committing_owner)
{
    if (child_frame.pending_navigation.has_value()) {
        auto const& pending_navigation = *child_frame.pending_navigation;
        if (pending_navigation.target_owner == committing_owner && pending_navigation.target_url == url)
            return true;

        dbgln_if(IFRAME_SITE_ISOLATION_DEBUG, "IFSO: rejecting child frame commit page={} frame={} url={} owner={} pending_owner={} pending_url={}",
            page_id,
            frame_id,
            url,
            child_frame_owner_name(committing_owner),
            child_frame_owner_name(pending_navigation.target_owner),
            pending_navigation.target_url);
        return false;
    }

    auto expected_owner = expected_child_frame_owner_for_navigation(parent_client, page_id, child_frame, url);
    if (expected_owner == committing_owner)
        return true;

    dbgln_if(IFRAME_SITE_ISOLATION_DEBUG, "IFSO: rejecting child frame commit page={} frame={} url={} owner={} expected_owner={}",
        page_id,
        frame_id,
        url,
        child_frame_owner_name(committing_owner),
        child_frame_owner_name(expected_owner));
    return false;
}

void SiteIsolationManager::record_pending_child_frame_navigation(u64 page_id, StringView frame_id, URL::URL const& url, ChildFrameOwner target_owner, Optional<u64> remote_page_id)
{
    auto child_frame = this->child_frame(page_id, frame_id);
    if (!child_frame.has_value())
        return;

    child_frame->pending_navigation = PendingChildFrameNavigation {
        .target_url = url,
        .target_owner = target_owner,
        .remote_page_id = remote_page_id,
    };
}

bool SiteIsolationManager::has_matching_pending_child_frame_navigation(u64 page_id, StringView frame_id, URL::URL const& url, ChildFrameOwner target_owner) const
{
    auto child_frame = this->child_frame(page_id, frame_id);
    if (!child_frame.has_value() || !child_frame->pending_navigation.has_value())
        return false;

    return child_frame->pending_navigation->target_owner == target_owner
        && child_frame->pending_navigation->target_url == url;
}

void SiteIsolationManager::clear_pending_child_frame_navigation(u64 page_id, StringView frame_id)
{
    auto child_frame = this->child_frame(page_id, frame_id);
    if (!child_frame.has_value())
        return;

    child_frame->pending_navigation.clear();
}

bool SiteIsolationManager::update_child_frame_committed_url(WebContentClient& committing_client, u64 page_id, StringView frame_id, URL::URL const& url)
{
    if (auto child_frame = this->child_frame(page_id, frame_id); child_frame.has_value()) {
        if (!is_child_frame_commit_allowed(committing_client, page_id, frame_id, *child_frame, url, ChildFrameOwner::Local))
            return false;

        if (child_frame->is_remote())
            transition_child_frame_to_local(committing_client, page_id, frame_id);

        child_frame->last_committed_url = url;
        child_frame->pending_navigation.clear();
        update_remote_child_frame_process_title(committing_client, *child_frame);
        return true;
    }

    auto parent_frame = parent_frame_for_remote_page(committing_client, page_id);
    if (!parent_frame.has_value())
        return false;

    if (!is_child_frame_commit_allowed(*parent_frame->parent_client, parent_frame->page_id, parent_frame->frame_id, *parent_frame->child_frame, url, ChildFrameOwner::Remote))
        return false;

    parent_frame->child_frame->last_committed_url = url;
    parent_frame->child_frame->pending_navigation.clear();
    update_remote_child_frame_process_title(*parent_frame->parent_client, *parent_frame->child_frame);
    return true;
}

bool SiteIsolationManager::remote_child_frame_did_finish_loading(WebContentClient& remote_client, u64 remote_page_id, URL::URL const& url)
{
    auto parent_frame = parent_frame_for_remote_page(remote_client, remote_page_id);
    if (!parent_frame.has_value())
        return false;

    if (!is_child_frame_commit_allowed(*parent_frame->parent_client, parent_frame->page_id, parent_frame->frame_id, *parent_frame->child_frame, url, ChildFrameOwner::Remote))
        return false;

    parent_frame->child_frame->last_committed_url = url;
    parent_frame->child_frame->pending_navigation.clear();
    update_remote_child_frame_process_title(*parent_frame->parent_client, *parent_frame->child_frame);
    parent_frame->parent_client->async_run_iframe_load_event_steps(parent_frame->page_id, parent_frame->frame_id);
    return true;
}

void SiteIsolationManager::update_remote_child_frame_process_title(WebContentClient& parent_client, WebContentClient& remote_client, URL::URL const& url)
{
    auto process = Application::the().find_process(remote_client.pid());
    if (!process.has_value())
        return;

    process->set_parent_pid(parent_client.pid());
    process->set_title(Utf16String::from_utf8(MUST(String::formatted("remote iframe: {}", url.serialize()))));
}

void SiteIsolationManager::update_remote_child_frame_process_title(WebContentClient& parent_client, ChildFrameHost const& child_frame, URL::URL const* url)
{
    if (!child_frame.is_remote())
        return;

    if (url) {
        update_remote_child_frame_process_title(parent_client, *child_frame.remote_client, *url);
        return;
    }

    auto title = "remote iframe"_string;
    if (child_frame.last_committed_url.has_value())
        title = MUST(String::formatted("remote iframe: {}", child_frame.last_committed_url->serialize()));

    auto process = Application::the().find_process(child_frame.remote_client->pid());
    if (!process.has_value())
        return;

    process->set_parent_pid(parent_client.pid());
    process->set_title(Utf16String::from_utf8(title));
}

bool SiteIsolationManager::dispatch_cursor_change_to_parent_frame(WebContentClient& remote_client, u64 page_id, Gfx::Cursor const& cursor)
{
    auto parent_frame = parent_frame_for_remote_page(remote_client, page_id);
    if (!parent_frame.has_value())
        return false;

    if (auto view = parent_frame->parent_client->view_for_page_id(parent_frame->page_id); view.has_value()) {
        if (view->on_cursor_change) {
            view->on_cursor_change(cursor);
            return true;
        }
    }

    return false;
}

void SiteIsolationManager::transition_child_frame_to_remote(
    WebContentClient& parent_client,
    u64 page_id,
    StringView frame_id,
    RefPtr<WebContentClient> remote_client,
    u64 remote_page_id,
    URL::URL const& url)
{
    auto child_frame = this->child_frame(page_id, frame_id);
    if (!child_frame.has_value())
        return;

    transition_child_frame_to_local(parent_client, page_id, frame_id);

    child_frame->owner = ChildFrameOwner::Remote;
    child_frame->remote_client = move(remote_client);
    child_frame->remote_page_id = remote_page_id;
    update_remote_child_frame_process_title(parent_client, *child_frame, &url);
}

void SiteIsolationManager::transition_child_frame_to_local(WebContentClient&, u64 page_id, StringView frame_id)
{
    auto child_frame = this->child_frame(page_id, frame_id);
    if (!child_frame.has_value())
        return;

    if (child_frame->is_remote()) {
        child_frame->remote_client->async_set_page_parent_context(child_frame->remote_page_id, {});
        child_frame->remote_client->request_close(child_frame->remote_page_id);
    }

    child_frame->owner = ChildFrameOwner::Local;
    child_frame->remote_client = nullptr;
    child_frame->remote_page_id = 0;

    if (auto focused_frame_id = m_focused_child_frames.get(page_id); focused_frame_id.has_value() && *focused_frame_id == frame_id)
        m_focused_child_frames.remove(page_id);
}

void SiteIsolationManager::close_remote_child_frames_for_page(WebContentClient& parent_client, u64 page_id)
{
    auto child_frames = m_child_frames.get(page_id);
    if (!child_frames.has_value())
        return;

    Vector<String> remote_child_frame_ids;
    for (auto const& child_frame_entry : *child_frames) {
        if (child_frame_entry.value.is_remote())
            remote_child_frame_ids.append(child_frame_entry.key);
    }

    for (auto const& frame_id : remote_child_frame_ids)
        transition_child_frame_to_local(parent_client, page_id, frame_id);
}

void SiteIsolationManager::close_all_remote_child_frames(WebContentClient& parent_client)
{
    Vector<u64> page_ids;
    for (auto const& page_child_frames : m_child_frames) {
        auto page_id = page_child_frames.key;
        if (client_owns_page(parent_client, page_id))
            page_ids.append(page_id);
    }

    for (auto page_id : page_ids)
        close_remote_child_frames_for_page(parent_client, page_id);
}

void SiteIsolationManager::remove_page(u64 page_id)
{
    m_child_frames.remove(page_id);
    m_focused_child_frames.remove(page_id);
}

void SiteIsolationManager::remove_all_pages_for_client(WebContentClient& client)
{
    Vector<u64> page_ids;
    for (auto const& page_child_frames : m_child_frames) {
        auto page_id = page_child_frames.key;
        if (client_owns_page(client, page_id))
            page_ids.append(page_id);
    }

    for (auto page_id : page_ids)
        remove_page(page_id);
}

Optional<String> SiteIsolationManager::focused_child_frame(u64 page_id) const
{
    auto focused_child_frame_id = m_focused_child_frames.get(page_id);
    if (!focused_child_frame_id.has_value())
        return {};
    return String { *focused_child_frame_id };
}

void SiteIsolationManager::set_focused_child_frame(u64 page_id, String const& frame_id)
{
    m_focused_child_frames.set(page_id, frame_id);
}

void SiteIsolationManager::clear_focused_child_frame(u64 page_id)
{
    m_focused_child_frames.remove(page_id);
}

String SiteIsolationManager::process_tree_for_testing(Optional<u64> root_page_id)
{
    Vector<WebContentClient*> sorted_clients;
    sorted_clients.ensure_capacity(WebContentClient::client_count());
    WebContentClient::for_each_client([&](auto& client) {
        sorted_clients.unchecked_append(&client);
        return IterationDecision::Continue;
    });
    quick_sort(sorted_clients, [](auto const* lhs, auto const* rhs) {
        return lhs->m_initial_page_id < rhs->m_initial_page_id;
    });

    Vector<WebContentClient*> clients_to_dump;
    auto ensure_client_is_in_dump = [&](WebContentClient& client) {
        if (!clients_to_dump.contains_slow(&client))
            clients_to_dump.append(&client);
    };

    Function<void(WebContentClient&)> include_remote_children = [&](WebContentClient& client) {
        for (auto const& page_child_frames : m_child_frames) {
            if (!client_owns_page(client, page_child_frames.key))
                continue;
            for (auto const& child_frame_entry : page_child_frames.value) {
                auto const& child_frame = child_frame_entry.value;
                if (!child_frame.is_remote())
                    continue;
                ensure_client_is_in_dump(*child_frame.remote_client);
                include_remote_children(*child_frame.remote_client);
            }
        }
    };

    if (root_page_id.has_value()) {
        for (auto* client : sorted_clients) {
            if (!client_owns_page(*client, *root_page_id))
                continue;
            ensure_client_is_in_dump(*client);
            include_remote_children(*client);
            break;
        }
    } else {
        for (auto* client : sorted_clients)
            ensure_client_is_in_dump(*client);
    }

    quick_sort(clients_to_dump, [](auto const* lhs, auto const* rhs) {
        return lhs->m_initial_page_id < rhs->m_initial_page_id;
    });

    auto process_index_for_client = [&](WebContentClient const& client) -> Optional<size_t> {
        for (size_t index = 0; index < clients_to_dump.size(); ++index) {
            if (clients_to_dump[index] == &client)
                return index;
        }
        return {};
    };

    auto url_or_none = [](Optional<URL::URL> const& url) -> String {
        if (url.has_value())
            return url->serialize();
        return "<none>"_string;
    };

    auto pending_navigation_or_empty = [](ChildFrameHost const& child_frame) -> String {
        if (!child_frame.pending_navigation.has_value())
            return ""_string;

        auto const& pending_navigation = *child_frame.pending_navigation;
        if (pending_navigation.remote_page_id.has_value()) {
            return MUST(String::formatted(" pending={} page={} url={}",
                child_frame_owner_name(pending_navigation.target_owner),
                *pending_navigation.remote_page_id,
                pending_navigation.target_url.serialize()));
        }

        return MUST(String::formatted(" pending={} url={}",
            child_frame_owner_name(pending_navigation.target_owner),
            pending_navigation.target_url.serialize()));
    };

    StringBuilder builder;
    if (clients_to_dump.is_empty()) {
        builder.append("no site isolation state\n"sv);
        return builder.to_string_without_validation();
    }

    for (size_t process_index = 0; process_index < clients_to_dump.size(); ++process_index) {
        auto& client = *clients_to_dump[process_index];
        builder.appendff("process {}: WebContent pid={}\n", process_index, client.pid());

        Vector<u64> page_ids;
        for (auto const& view_entry : client.m_views)
            page_ids.append(view_entry.key);
        quick_sort(page_ids);
        for (auto page_id : page_ids) {
            auto view = client.m_views.get(page_id);
            VERIFY(view.has_value());
            builder.appendff("  page {}: {}\n", page_id, (*view)->url().serialize());
        }

        Vector<u64> embedded_page_ids;
        for (auto page_id : client.m_embedded_pages)
            embedded_page_ids.append(page_id);
        quick_sort(embedded_page_ids);
        for (auto page_id : embedded_page_ids)
            builder.appendff("  embedded-page {}\n", page_id);

        struct ChildFrameDump {
            String frame_id;
            ChildFrameHost const* host { nullptr };
        };

        Vector<u64> child_frame_page_ids;
        for (auto const& page_child_frames : m_child_frames) {
            if (client_owns_page(client, page_child_frames.key))
                child_frame_page_ids.append(page_child_frames.key);
        }
        quick_sort(child_frame_page_ids);

        for (auto page_id : child_frame_page_ids) {
            auto child_frames = m_child_frames.get(page_id);
            VERIFY(child_frames.has_value());

            Vector<ChildFrameDump> sorted_child_frames;
            for (auto const& child_frame_entry : *child_frames)
                sorted_child_frames.append({ child_frame_entry.key, &child_frame_entry.value });
            quick_sort(sorted_child_frames, [](auto const& lhs, auto const& rhs) {
                return lhs.frame_id < rhs.frame_id;
            });

            for (auto const& child_frame : sorted_child_frames) {
                if (!child_frame.host->is_remote()) {
                    builder.appendff("  frame {}: local process={} pid={} page={} url={}{}\n",
                        child_frame.frame_id,
                        process_index,
                        client.pid(),
                        page_id,
                        url_or_none(child_frame.host->last_committed_url),
                        pending_navigation_or_empty(*child_frame.host));
                    continue;
                }

                auto remote_process_index = process_index_for_client(*child_frame.host->remote_client);
                builder.appendff("  frame {}: remote process={} pid={} page={} url={}{}\n",
                    child_frame.frame_id,
                    remote_process_index.has_value() ? String::number(*remote_process_index) : "<unknown>"_string,
                    child_frame.host->remote_client->pid(),
                    child_frame.host->remote_page_id,
                    url_or_none(child_frame.host->last_committed_url),
                    pending_navigation_or_empty(*child_frame.host));
            }
        }
    }

    return builder.to_string_without_validation();
}

}
