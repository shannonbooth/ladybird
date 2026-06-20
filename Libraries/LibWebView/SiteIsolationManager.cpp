/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/SiteIsolationManager.h>

#include <AK/QuickSort.h>
#include <AK/StringBuilder.h>
#include <LibWeb/Page/ViewportIsFullscreen.h>
#include <LibWebView/SiteIsolation.h>
#include <LibWebView/ViewImplementation.h>
#include <LibWebView/WebContentClient.h>

namespace WebView {

SiteIsolationManager& SiteIsolationManager::the()
{
    static auto& manager = *new SiteIsolationManager;
    return manager;
}

void SiteIsolationManager::ensure_top_level_navigable_record(u64 page_id, StringView navigable_id)
{
    auto& navigation_tree = m_navigation_trees.ensure(page_id, [] {
        return HashMap<String, NavigableRecord> {};
    });

    auto id = MUST(String::from_utf8(navigable_id));
    if (navigation_tree.contains(id))
        return;

    NavigableRecord top_level_navigable;
    navigation_tree.set(move(id), move(top_level_navigable));
}

void SiteIsolationManager::did_create_child_frame(u64 page_id, String parent_navigable_id, String frame_id)
{
    ensure_top_level_navigable_record(page_id, parent_navigable_id);

    auto& navigation_tree = m_navigation_trees.ensure(page_id, [] {
        return HashMap<String, NavigableRecord> {};
    });
    NavigableRecord child_frame;
    child_frame.parent_navigable_id = move(parent_navigable_id);
    navigation_tree.set(move(frame_id), move(child_frame));
}

Web::NavigationProcessDecision SiteIsolationManager::decide_navigation_process(WebContentClient& parent_client, u64 page_id, Optional<String> frame_id, URL::URL current_url, URL::URL target_url, Web::NavigationTarget target)
{
    if (target == Web::NavigationTarget::IFrame && frame_id.has_value()) {
        if (auto child_frame = this->child_frame(page_id, *frame_id); child_frame.has_value())
            current_url = embedding_page_url_for_child_frame_navigation(parent_client, page_id, *child_frame, current_url);
    }

    auto decision = WebView::is_url_suitable_for_same_process_navigation(current_url, target_url, target)
        ? Web::NavigationProcessDecision::Local
        : Web::NavigationProcessDecision::Remote;

    if (target == Web::NavigationTarget::IFrame && frame_id.has_value()) {
        auto target_owner = decision == Web::NavigationProcessDecision::Local
            ? ChildFrameOwner::Local
            : ChildFrameOwner::Remote;
        record_pending_child_frame_navigation(page_id, *frame_id, target_url, target_owner);

        if (target_owner == ChildFrameOwner::Local)
            transition_child_frame_to_local(parent_client, page_id, *frame_id);
    }

    return decision;
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

bool SiteIsolationManager::did_commit_child_frame_navigation(WebContentClient& parent_client, u64 page_id, StringView frame_id, URL::URL const& url)
{
    auto child_frame = this->child_frame(page_id, frame_id);
    if (!child_frame.has_value())
        return false;

    if (child_frame->is_remote())
        transition_child_frame_to_local(parent_client, page_id, frame_id);

    child_frame->last_committed_url = url;
    child_frame->pending_navigation.clear();
    return true;
}

void SiteIsolationManager::did_destroy_child_frame(WebContentClient& parent_client, u64 page_id, StringView frame_id)
{
    auto navigation_tree = m_navigation_trees.get(page_id);
    if (!navigation_tree.has_value())
        return;

    transition_child_frame_to_local(parent_client, page_id, frame_id);

    navigation_tree->remove(frame_id);
    if (navigation_tree->is_empty())
        m_navigation_trees.remove(page_id);
}

Optional<SiteIsolationManager::RemoteChildFrameInputTarget> SiteIsolationManager::remote_child_frame_input_target_at(u64 page_id, Web::DevicePixelPoint position) const
{
    auto navigation_tree = m_navigation_trees.get(page_id);
    if (!navigation_tree.has_value())
        return {};

    for (auto const& child_frame_entry : *navigation_tree) {
        auto const& child_frame = child_frame_entry.value;
        if (!child_frame.is_remote() || !child_frame.viewport_rect.has_value())
            continue;
        if (!child_frame.viewport_rect->contains(position))
            continue;

        return RemoteChildFrameInputTarget {
            .remote_client = child_frame.remote_client,
            .remote_page_id = child_frame.remote_page_id,
            .viewport_rect = *child_frame.viewport_rect,
        };
    }

    return {};
}

bool SiteIsolationManager::remote_child_frame_did_finish_loading(WebContentClient& remote_client, u64 remote_page_id, URL::URL const& url)
{
    auto parent_frame = parent_frame_for_remote_page(remote_client, remote_page_id);
    if (!parent_frame.has_value())
        return false;

    parent_frame->child_frame->last_committed_url = url;
    parent_frame->child_frame->pending_navigation.clear();
    parent_frame->parent_client->async_run_iframe_load_event_steps(parent_frame->page_id, parent_frame->frame_id);
    return true;
}

bool SiteIsolationManager::remote_child_frame_did_commit_navigation(WebContentClient& remote_client, u64 remote_page_id, URL::URL const& url)
{
    auto parent_frame = parent_frame_for_remote_page(remote_client, remote_page_id);
    if (!parent_frame.has_value())
        return false;

    parent_frame->child_frame->last_committed_url = url;
    parent_frame->child_frame->pending_navigation.clear();
    return true;
}

bool SiteIsolationManager::remote_child_frame_did_finish_handling_input_event(WebContentClient& remote_client, u64 remote_page_id, Web::EventResult event_result)
{
    auto parent_frame = parent_frame_for_remote_page(remote_client, remote_page_id);
    if (!parent_frame.has_value())
        return false;

    parent_frame->parent_client->did_finish_handling_input_event(parent_frame->page_id, event_result);
    return true;
}

void SiteIsolationManager::remove_page(u64 page_id)
{
    m_navigation_trees.remove(page_id);
}

void SiteIsolationManager::remove_all_pages_for_client(WebContentClient& client)
{
    Vector<u64> page_ids;
    for (auto const& page_navigation_tree : m_navigation_trees) {
        auto page_id = page_navigation_tree.key;
        if (client_owns_page(client, page_id))
            page_ids.append(page_id);
    }

    for (auto page_id : page_ids)
        remove_page(page_id);
}

String SiteIsolationManager::dump_process_tree(WebContentClient& client, u64 page_id) const
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

    auto sorted_top_level_navigable_ids = [&](u64 page_id) {
        Vector<String> top_level_navigable_ids;
        auto navigation_tree = m_navigation_trees.get(page_id);
        if (!navigation_tree.has_value())
            return top_level_navigable_ids;

        for (auto const& navigable_entry : *navigation_tree) {
            if (navigable_entry.value.parent_navigable_id.is_empty())
                top_level_navigable_ids.append(navigable_entry.key);
        }

        quick_sort(top_level_navigable_ids);
        return top_level_navigable_ids;
    };

    auto sorted_child_frame_ids = [&](u64 page_id, StringView parent_navigable_id) {
        Vector<String> child_frame_ids;
        auto navigation_tree = m_navigation_trees.get(page_id);
        if (!navigation_tree.has_value())
            return child_frame_ids;

        for (auto const& child_frame_entry : *navigation_tree) {
            auto const& child_frame = child_frame_entry.value;
            if (child_frame.parent_navigable_id == parent_navigable_id)
                child_frame_ids.append(child_frame_entry.key);
        }

        quick_sort(child_frame_ids);
        return child_frame_ids;
    };

    Function<void(WebContentClient&, u64, size_t)> dump_page_tree;
    Function<void(WebContentClient&, u64, StringView, size_t)> dump_frame_tree;
    dump_frame_tree = [&](WebContentClient& current_client, u64 current_page_id, StringView parent_navigable_id, size_t depth) {
        auto navigation_tree = m_navigation_trees.get(current_page_id);
        if (!navigation_tree.has_value())
            return;

        auto child_frame_ids = sorted_child_frame_ids(current_page_id, parent_navigable_id);
        for (size_t i = 0; i < child_frame_ids.size(); ++i) {
            auto child_frame = navigation_tree->get(child_frame_ids[i]);
            VERIFY(child_frame.has_value());

            builder.append_repeated(' ', depth * 2);
            builder.appendff("iframe#{}: {}", i, child_frame->is_remote() ? "remote"sv : "local"sv);
            if (child_frame->is_remote())
                builder.appendff(" WebContent#{}", process_index(*child_frame->remote_client));
            builder.append('\n');

            if (child_frame->is_remote())
                dump_page_tree(*child_frame->remote_client, child_frame->remote_page_id, depth + 1);
            else
                dump_frame_tree(current_client, current_page_id, child_frame_ids[i].bytes_as_string_view(), depth + 1);
        }
    };
    dump_page_tree = [&](WebContentClient& current_client, u64 current_page_id, size_t depth) {
        auto top_level_navigable_ids = sorted_top_level_navigable_ids(current_page_id);
        for (auto const& top_level_navigable_id : top_level_navigable_ids)
            dump_frame_tree(current_client, current_page_id, top_level_navigable_id.bytes_as_string_view(), depth);
    };

    builder.appendff("WebContent#{}\n", process_index(client));
    dump_page_tree(client, page_id, 1);
    return builder.to_string_without_validation();
}

HashMap<pid_t, pid_t> SiteIsolationManager::remote_frame_process_embedders() const
{
    HashMap<pid_t, pid_t> embedders;

    for (auto const& page_navigation_tree : m_navigation_trees) {
        WebContentClient* embedder_client = nullptr;
        WebContentClient::for_each_client([&](auto& client) {
            if (!client_owns_page(client, page_navigation_tree.key))
                return IterationDecision::Continue;

            embedder_client = &client;
            return IterationDecision::Break;
        });
        if (!embedder_client)
            continue;

        for (auto const& child_frame_entry : page_navigation_tree.value) {
            auto const& child_frame = child_frame_entry.value;
            if (!child_frame.is_remote())
                continue;

            embedders.set(child_frame.remote_client->pid(), embedder_client->pid());
        }
    }

    return embedders;
}

bool SiteIsolationManager::has_matching_pending_child_frame_navigation(u64 page_id, StringView frame_id, URL::URL const& url, ChildFrameOwner target_owner) const
{
    auto child_frame = this->child_frame(page_id, frame_id);
    if (!child_frame.has_value() || !child_frame->pending_navigation.has_value())
        return false;

    return child_frame->pending_navigation->target_owner == target_owner
        && child_frame->pending_navigation->target_url == url;
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

void SiteIsolationManager::clear_pending_child_frame_navigation(u64 page_id, StringView frame_id)
{
    auto child_frame = this->child_frame(page_id, frame_id);
    if (!child_frame.has_value())
        return;

    child_frame->pending_navigation.clear();
}

void SiteIsolationManager::transition_child_frame_to_remote(WebContentClient& parent_client, u64 page_id, StringView frame_id, RefPtr<WebContentClient> remote_client, u64 remote_page_id)
{
    auto child_frame = this->child_frame(page_id, frame_id);
    if (!child_frame.has_value())
        return;

    transition_child_frame_to_local(parent_client, page_id, frame_id);

    child_frame->owner = ChildFrameOwner::Remote;
    child_frame->remote_client = move(remote_client);
    child_frame->remote_page_id = remote_page_id;
    parent_client.async_set_remote_child_frame_compositor_context(
        page_id,
        MUST(String::from_utf8(frame_id)),
        Web::Compositor::compositor_context_id_for_page(remote_page_id));
}

void SiteIsolationManager::transition_child_frame_to_local(WebContentClient& parent_client, u64 page_id, StringView frame_id)
{
    auto child_frame = this->child_frame(page_id, frame_id);
    if (!child_frame.has_value())
        return;

    parent_client.async_set_remote_child_frame_compositor_context(page_id, MUST(String::from_utf8(frame_id)), {});

    if (child_frame->is_remote()) {
        child_frame->remote_client->async_set_page_parent_context(child_frame->remote_page_id, {});
        child_frame->remote_client->request_close(child_frame->remote_page_id);
    }

    child_frame->owner = ChildFrameOwner::Local;
    child_frame->remote_client = nullptr;
    child_frame->remote_page_id = 0;
}

void SiteIsolationManager::close_remote_child_frames_for_page(WebContentClient& parent_client, u64 page_id)
{
    auto navigation_tree = m_navigation_trees.get(page_id);
    if (!navigation_tree.has_value())
        return;

    Vector<String> remote_child_frame_ids;
    for (auto const& child_frame_entry : *navigation_tree) {
        if (child_frame_entry.value.is_remote())
            remote_child_frame_ids.append(child_frame_entry.key);
    }

    for (auto const& frame_id : remote_child_frame_ids)
        transition_child_frame_to_local(parent_client, page_id, frame_id);
}

Optional<SiteIsolationManager::ChildFrameHost&> SiteIsolationManager::child_frame(u64 page_id, StringView frame_id)
{
    auto navigation_tree = m_navigation_trees.get(page_id);
    if (!navigation_tree.has_value())
        return {};

    return navigation_tree->get(frame_id);
}

Optional<SiteIsolationManager::ChildFrameHost const&> SiteIsolationManager::child_frame(u64 page_id, StringView frame_id) const
{
    auto navigation_tree = m_navigation_trees.get(page_id);
    if (!navigation_tree.has_value())
        return {};

    return navigation_tree->get(frame_id);
}

bool SiteIsolationManager::client_owns_page(WebContentClient const& client, u64 page_id)
{
    return client.m_views.contains(page_id) || client.m_embedded_pages.contains(page_id);
}

Optional<SiteIsolationManager::ParentFrame> SiteIsolationManager::parent_frame_for_remote_page(WebContentClient& remote_client, u64 remote_page_id)
{
    Optional<ParentFrame> result;

    WebContentClient::for_each_client([&](auto& parent_client) {
        for (auto& page_navigation_tree : m_navigation_trees) {
            auto parent_page_id = page_navigation_tree.key;
            if (!client_owns_page(parent_client, parent_page_id))
                continue;

            for (auto& child_frame_entry : page_navigation_tree.value) {
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

Optional<URL::URL> SiteIsolationManager::document_url_for_child_frame(NavigableRecord const& child_frame)
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

URL::URL SiteIsolationManager::embedding_page_url_for_child_frame_navigation(WebContentClient& parent_client, u64 page_id, NavigableRecord const& child_frame, URL::URL const& fallback_url)
{
    if (auto parent_frame = this->child_frame(page_id, child_frame.parent_navigable_id); parent_frame.has_value()) {
        if (auto url = document_url_for_child_frame(*parent_frame); url.has_value())
            return *url;
    }

    return document_url_for_page(parent_client, page_id, fallback_url);
}

}
