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

void SiteIsolationManager::did_create_child_frame(WebContentClient& parent_client, u64 page_id, String parent_frame_id, String frame_id)
{
    auto traversable = canonical_traversable_for_page(parent_client, page_id);
    if (!traversable.has_value())
        return;

    traversable->create_child_navigable(move(parent_frame_id), move(frame_id), parent_client, page_id);
}

Web::NavigationProcessDecision SiteIsolationManager::decide_navigation_process(WebContentClient& parent_client, u64 page_id, Optional<String> frame_id, URL::URL current_url, URL::URL target_url, Web::NavigationTarget target)
{
    if (target == Web::NavigationTarget::IFrame && frame_id.has_value()) {
        if (auto child_navigable = this->child_navigable(page_id, *frame_id); child_navigable.has_value())
            current_url = embedding_page_url_for_child_frame_navigation(parent_client, page_id, *child_navigable, current_url);
    }

    auto decision = WebView::is_url_suitable_for_same_process_navigation(current_url, target_url, target)
        ? Web::NavigationProcessDecision::Local
        : Web::NavigationProcessDecision::Remote;

    if (target == Web::NavigationTarget::IFrame && frame_id.has_value()) {
        auto target_host = decision == Web::NavigationProcessDecision::Local
            ? CanonicalNavigable::PendingNavigationHost::Local
            : CanonicalNavigable::PendingNavigationHost::Remote;
        record_pending_child_frame_navigation(page_id, *frame_id, target_url, target_host);

        if (target_host == CanonicalNavigable::PendingNavigationHost::Local)
            transition_child_frame_to_local(parent_client, page_id, *frame_id);
    }

    return decision;
}

void SiteIsolationManager::did_update_child_frame_viewport(u64 page_id, String frame_id, Web::DevicePixelRect viewport_rect, double device_pixel_ratio)
{
    auto child_navigable = this->child_navigable(page_id, frame_id);
    if (!child_navigable.has_value())
        return;

    child_navigable->set_viewport_rect(viewport_rect, device_pixel_ratio);
    if (child_navigable->active_document_is_remote()) {
        child_navigable->remote_active_document_client()->async_set_viewport(
            child_navigable->remote_active_document_page_id(),
            viewport_rect.size(),
            device_pixel_ratio,
            Web::ViewportIsFullscreen::No);
    }
}

bool SiteIsolationManager::did_commit_child_frame_navigation(WebContentClient& parent_client, u64 page_id, StringView frame_id, URL::URL const& url)
{
    auto child_navigable = this->child_navigable(page_id, frame_id);
    if (!child_navigable.has_value())
        return false;

    if (child_navigable->active_document_is_remote())
        transition_child_frame_to_local(parent_client, page_id, frame_id);

    child_navigable->set_active_document_url(url);
    child_navigable->clear_pending_child_frame_navigation();
    return true;
}

void SiteIsolationManager::did_destroy_child_frame(WebContentClient& parent_client, u64 page_id, StringView frame_id)
{
    auto traversable = canonical_traversable_for_page(parent_client, page_id);
    if (!traversable.has_value())
        return;

    transition_child_frame_to_local(parent_client, page_id, frame_id);

    traversable->destroy_child_navigable(frame_id);
}

Optional<SiteIsolationManager::RemoteChildFrameInputTarget> SiteIsolationManager::remote_child_frame_input_target_at(u64 page_id, Web::DevicePixelPoint position) const
{
    auto traversable = canonical_traversable_for_page(page_id);
    if (!traversable.has_value())
        return {};

    Optional<RemoteChildFrameInputTarget> target;
    traversable->for_each_child_navigable([&](auto const&, auto const& child_navigable) {
        if (child_navigable.embedding_page_id() != page_id)
            return IterationDecision::Continue;
        if (!child_navigable.active_document_is_remote() || !child_navigable.viewport_rect().has_value())
            return IterationDecision::Continue;
        if (!child_navigable.viewport_rect()->contains(position))
            return IterationDecision::Continue;

        target = RemoteChildFrameInputTarget {
            .remote_client = child_navigable.remote_active_document_client(),
            .remote_page_id = child_navigable.remote_active_document_page_id(),
            .viewport_rect = *child_navigable.viewport_rect(),
        };
        return IterationDecision::Break;
    });

    return target;
}

bool SiteIsolationManager::remote_child_frame_did_finish_loading(WebContentClient& remote_client, u64 remote_page_id, URL::URL const& url)
{
    auto parent_frame = parent_frame_for_remote_page(remote_client, remote_page_id);
    if (!parent_frame.has_value())
        return false;

    parent_frame->child_navigable->set_active_document_url(url);
    parent_frame->child_navigable->clear_pending_child_frame_navigation();
    parent_frame->parent_client->async_run_iframe_load_event_steps(parent_frame->page_id, parent_frame->frame_id);
    return true;
}

bool SiteIsolationManager::remote_child_frame_did_commit_navigation(WebContentClient& remote_client, u64 remote_page_id, URL::URL const& url)
{
    auto parent_frame = parent_frame_for_remote_page(remote_client, remote_page_id);
    if (!parent_frame.has_value())
        return false;

    parent_frame->child_navigable->set_active_document_url(url);
    parent_frame->child_navigable->clear_pending_child_frame_navigation();
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
    m_page_traversables.remove(page_id);
}

void SiteIsolationManager::remove_all_pages_for_client(WebContentClient& client)
{
    Vector<u64> page_ids;
    Vector<CanonicalTraversable const*> owned_traversables;
    for (auto const& view : client.m_views)
        owned_traversables.append(&view.value->m_top_level_traversable);

    for (auto const& page_traversable : m_page_traversables) {
        auto page_id = page_traversable.key;
        if (client_owns_page(client, page_id) || owned_traversables.contains_slow(page_traversable.value))
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

    auto sorted_child_frame_ids = [](CanonicalTraversable const& traversable, u64 page_id, Optional<StringView> parent_frame_id) {
        Vector<String> child_frame_ids;
        traversable.for_each_child_navigable([&](auto const& child_id, auto const& child_navigable) {
            if (child_navigable.embedding_page_id() != page_id)
                return IterationDecision::Continue;

            auto parent_frame_exists_on_page = false;
            traversable.for_each_child_navigable([&](auto const& possible_parent_id, auto const& possible_parent_navigable) {
                if (possible_parent_navigable.embedding_page_id() != page_id || possible_parent_id != child_navigable.parent_id())
                    return IterationDecision::Continue;
                parent_frame_exists_on_page = true;
                return IterationDecision::Break;
            });

            auto is_child_of_parent_frame = parent_frame_id.has_value()
                ? child_navigable.parent_id() == *parent_frame_id
                : !parent_frame_exists_on_page;
            if (is_child_of_parent_frame)
                child_frame_ids.append(child_id);
            return IterationDecision::Continue;
        });

        quick_sort(child_frame_ids);
        return child_frame_ids;
    };

    Function<void(WebContentClient&, u64, Optional<StringView>, size_t)> dump_frame_tree;
    dump_frame_tree = [&](WebContentClient& current_client, u64 current_page_id, Optional<StringView> parent_frame_id, size_t depth) {
        auto traversable = canonical_traversable_for_page(current_client, current_page_id);
        if (!traversable.has_value())
            return;

        auto child_frame_ids = sorted_child_frame_ids(*traversable, current_page_id, parent_frame_id);
        for (size_t i = 0; i < child_frame_ids.size(); ++i) {
            auto child_navigable = traversable->child_navigable(child_frame_ids[i]);
            VERIFY(child_navigable.has_value());

            builder.append_repeated(' ', depth * 2);
            builder.appendff("iframe#{}: {}", i, child_navigable->active_document_is_remote() ? "remote"sv : "local"sv);
            if (child_navigable->active_document_is_remote())
                builder.appendff(" WebContent#{}", process_index(*child_navigable->remote_active_document_client()));
            builder.append('\n');

            if (child_navigable->active_document_is_remote())
                dump_frame_tree(*child_navigable->remote_active_document_client(), child_navigable->remote_active_document_page_id(), {}, depth + 1);
            else
                dump_frame_tree(current_client, current_page_id, child_frame_ids[i].bytes_as_string_view(), depth + 1);
        }
    };

    builder.appendff("WebContent#{}\n", process_index(client));
    dump_frame_tree(client, page_id, {}, 1);
    return builder.to_string_without_validation();
}

HashMap<pid_t, pid_t> SiteIsolationManager::remote_frame_process_embedders() const
{
    HashMap<pid_t, pid_t> embedders;
    Vector<CanonicalTraversable const*> traversables;

    for (auto const& page_traversable : m_page_traversables) {
        if (traversables.contains_slow(page_traversable.value))
            continue;
        traversables.append(page_traversable.value);

        page_traversable.value->for_each_child_navigable([&](auto const&, auto const& child_navigable) {
            if (!child_navigable.active_document_is_remote() || !child_navigable.embedding_client())
                return IterationDecision::Continue;

            embedders.set(child_navigable.remote_active_document_client()->pid(), child_navigable.embedding_client()->pid());
            return IterationDecision::Continue;
        });
    }

    return embedders;
}

bool SiteIsolationManager::has_matching_pending_child_frame_navigation(u64 page_id, StringView frame_id, URL::URL const& url, CanonicalNavigable::PendingNavigationHost target_host) const
{
    auto child_navigable = this->child_navigable(page_id, frame_id);
    if (!child_navigable.has_value() || !child_navigable->pending_child_frame_navigation().has_value())
        return false;

    return child_navigable->pending_child_frame_navigation()->target_host == target_host
        && child_navigable->pending_child_frame_navigation()->target_url == url;
}

void SiteIsolationManager::record_pending_child_frame_navigation(u64 page_id, StringView frame_id, URL::URL const& url, CanonicalNavigable::PendingNavigationHost target_host)
{
    auto child_navigable = this->child_navigable(page_id, frame_id);
    if (!child_navigable.has_value())
        return;

    child_navigable->set_pending_child_frame_navigation(url, target_host);
}

void SiteIsolationManager::clear_pending_child_frame_navigation(u64 page_id, StringView frame_id)
{
    auto child_navigable = this->child_navigable(page_id, frame_id);
    if (!child_navigable.has_value())
        return;

    child_navigable->clear_pending_child_frame_navigation();
}

void SiteIsolationManager::transition_child_frame_to_remote(WebContentClient& parent_client, u64 page_id, StringView frame_id, RefPtr<WebContentClient> remote_client, u64 remote_page_id)
{
    auto child_navigable = this->child_navigable(page_id, frame_id);
    if (!child_navigable.has_value())
        return;

    transition_child_frame_to_local(parent_client, page_id, frame_id);

    if (auto traversable = canonical_traversable_for_page(parent_client, page_id); traversable.has_value())
        m_page_traversables.set(remote_page_id, &*traversable);

    child_navigable->set_active_document_host(*remote_client, remote_page_id);
    parent_client.async_set_remote_child_frame_compositor_context(
        page_id,
        MUST(String::from_utf8(frame_id)),
        Web::Compositor::compositor_context_id_for_page(remote_page_id));
}

void SiteIsolationManager::transition_child_frame_to_local(WebContentClient& parent_client, u64 page_id, StringView frame_id)
{
    auto child_navigable = this->child_navigable(page_id, frame_id);
    if (!child_navigable.has_value())
        return;

    parent_client.async_set_remote_child_frame_compositor_context(page_id, MUST(String::from_utf8(frame_id)), {});

    if (child_navigable->active_document_is_remote()) {
        auto remote_client = child_navigable->remote_active_document_client();
        auto remote_page_id = child_navigable->remote_active_document_page_id();
        m_page_traversables.remove(remote_page_id);
        remote_client->async_set_page_parent_context(remote_page_id, {});
        remote_client->request_close(remote_page_id);
    }

    if (child_navigable->embedding_client())
        child_navigable->set_active_document_host(*child_navigable->embedding_client(), child_navigable->embedding_page_id());
    else
        child_navigable->clear_active_document_host();
}

void SiteIsolationManager::close_remote_child_frames_for_page(WebContentClient& parent_client, u64 page_id)
{
    auto traversable = canonical_traversable_for_page(parent_client, page_id);
    if (!traversable.has_value())
        return;

    Vector<String> remote_child_frame_ids;
    traversable->for_each_child_navigable([&](auto const& child_id, auto const& child_navigable) {
        if (child_navigable.embedding_page_id() == page_id && child_navigable.active_document_is_remote())
            remote_child_frame_ids.append(child_id);
        return IterationDecision::Continue;
    });

    for (auto const& frame_id : remote_child_frame_ids)
        transition_child_frame_to_local(parent_client, page_id, frame_id);
}

Optional<CanonicalNavigable&> SiteIsolationManager::child_navigable(u64 page_id, StringView frame_id)
{
    auto traversable = canonical_traversable_for_page(page_id);
    if (!traversable.has_value())
        return {};
    return traversable->child_navigable(frame_id);
}

Optional<CanonicalNavigable const&> SiteIsolationManager::child_navigable(u64 page_id, StringView frame_id) const
{
    auto traversable = canonical_traversable_for_page(page_id);
    if (!traversable.has_value())
        return {};
    return traversable->child_navigable(frame_id);
}

bool SiteIsolationManager::client_owns_page(WebContentClient const& client, u64 page_id)
{
    return client.m_views.contains(page_id) || client.m_embedded_pages.contains(page_id);
}

Optional<CanonicalTraversable&> SiteIsolationManager::canonical_traversable_for_page(WebContentClient& client, u64 page_id)
{
    if (auto view = client.m_views.get(page_id); view.has_value())
        return (*view)->m_top_level_traversable;

    if (auto traversable = m_page_traversables.get(page_id); traversable.has_value())
        return **traversable;

    return {};
}

Optional<CanonicalTraversable const&> SiteIsolationManager::canonical_traversable_for_page(WebContentClient const& client, u64 page_id) const
{
    if (auto view = client.m_views.get(page_id); view.has_value())
        return (*view)->m_top_level_traversable;

    if (auto traversable = m_page_traversables.get(page_id); traversable.has_value())
        return **traversable;

    return {};
}

Optional<CanonicalTraversable&> SiteIsolationManager::canonical_traversable_for_page(u64 page_id)
{
    if (auto traversable = m_page_traversables.get(page_id); traversable.has_value())
        return **traversable;

    Optional<CanonicalTraversable&> traversable;
    WebContentClient::for_each_client([&](auto& client) {
        if (auto view = client.m_views.get(page_id); view.has_value()) {
            traversable = (*view)->m_top_level_traversable;
            return IterationDecision::Break;
        }
        return IterationDecision::Continue;
    });
    return traversable;
}

Optional<CanonicalTraversable const&> SiteIsolationManager::canonical_traversable_for_page(u64 page_id) const
{
    if (auto traversable = m_page_traversables.get(page_id); traversable.has_value())
        return **traversable;

    Optional<CanonicalTraversable const&> traversable;
    WebContentClient::for_each_client([&](auto& client) {
        if (auto view = client.m_views.get(page_id); view.has_value()) {
            traversable = (*view)->m_top_level_traversable;
            return IterationDecision::Break;
        }
        return IterationDecision::Continue;
    });
    return traversable;
}

Optional<SiteIsolationManager::ParentFrame> SiteIsolationManager::parent_frame_for_remote_page(WebContentClient& remote_client, u64 remote_page_id)
{
    auto traversable = canonical_traversable_for_page(remote_page_id);
    if (!traversable.has_value())
        return {};

    Optional<ParentFrame> result;
    traversable->for_each_child_navigable([&](String const& frame_id, CanonicalNavigable& child_navigable) {
        auto child_frame_remote_client = child_navigable.remote_active_document_client();
        if (!child_navigable.active_document_is_remote() || child_frame_remote_client.ptr() != &remote_client || child_navigable.remote_active_document_page_id() != remote_page_id)
            return IterationDecision::Continue;

        result = ParentFrame {
            .parent_client = child_navigable.embedding_client(),
            .page_id = child_navigable.embedding_page_id(),
            .frame_id = frame_id,
            .child_navigable = &child_navigable,
        };
        return IterationDecision::Break;
    });

    if (result.has_value() && !result->parent_client)
        return {};

    return result;
}

Optional<URL::URL> SiteIsolationManager::document_url_for_child_frame(CanonicalNavigable const& child_navigable)
{
    if (child_navigable.active_document_url().has_value())
        return child_navigable.active_document_url();

    if (child_navigable.pending_child_frame_navigation().has_value())
        return child_navigable.pending_child_frame_navigation()->target_url;

    return {};
}

URL::URL SiteIsolationManager::document_url_for_page(WebContentClient& client, u64 page_id, URL::URL const& fallback_url)
{
    if (auto view = client.view_for_page_id(page_id); view.has_value())
        return view->url();

    if (client.m_embedded_pages.contains(page_id)) {
        if (auto parent_frame = parent_frame_for_remote_page(client, page_id); parent_frame.has_value()) {
            if (auto url = document_url_for_child_frame(*parent_frame->child_navigable); url.has_value())
                return *url;
        }
    }

    return fallback_url;
}

URL::URL SiteIsolationManager::embedding_page_url_for_child_frame_navigation(WebContentClient& parent_client, u64 page_id, CanonicalNavigable const& child_navigable, URL::URL const& fallback_url)
{
    if (auto parent_frame = this->child_navigable(page_id, child_navigable.parent_id()); parent_frame.has_value()) {
        if (auto url = document_url_for_child_frame(*parent_frame); url.has_value())
            return *url;
    }

    return document_url_for_page(parent_client, page_id, fallback_url);
}

}
