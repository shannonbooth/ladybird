/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/SiteIsolationManager.h>

#include <AK/StringBuilder.h>
#include <LibWeb/HTML/BrowsingContext.h>
#include <LibWebView/Application.h>
#include <LibWebView/CanonicalBrowsingContext.h>
#include <LibWebView/CanonicalBrowsingContextGroup.h>
#include <LibWebView/CanonicalTraversable.h>
#include <LibWebView/SiteIsolation.h>
#include <LibWebView/ViewImplementation.h>
#include <LibWebView/WebContentClient.h>

namespace WebView {

SiteIsolationManager& SiteIsolationManager::the()
{
    static auto& manager = *new SiteIsolationManager;
    return manager;
}

void SiteIsolationManager::remove_page(WebContentPage& page)
{
    if (!page.is_open())
        return;
    auto& traversable = page.traversable();

    if (traversable.is_displaced_document_host(page))
        traversable.forget_displaced_document_host({});
    traversable.forget_representing_page(page);

    Vector<Web::HTML::CrossProcessId> reported_by_page;
    Vector<Web::HTML::CrossProcessId> hosted_by_page;
    Vector<Web::HTML::CrossProcessId> pending_in_page;
    traversable.for_each_in_subtree([&](CanonicalNavigable const& navigable) {
        if (navigable.reporting_page().ptr() == &page)
            reported_by_page.append(navigable.id());
        if (navigable.has_remote_host() && &navigable.remote_host() == &page)
            hosted_by_page.append(navigable.id());
        if (navigable.pending_host_matches(page))
            pending_in_page.append(navigable.id());
        return IterationDecision::Continue;
    });

    for (auto navigable_id : pending_in_page) {
        auto navigable = traversable.find(navigable_id);
        if (!navigable.has_value())
            continue;
        if (navigable_id == traversable.id())
            navigable->clear_pending_host();
        else
            navigable->discard_pending_host();
    }

    for (auto navigable_id : reported_by_page) {
        if (auto navigable = traversable.find(navigable_id); navigable.has_value())
            remove_child_frame_subtree(*navigable);
    }

    for (auto navigable_id : hosted_by_page) {
        if (auto navigable = traversable.find(navigable_id); navigable.has_value())
            transition_child_frame_to_local(*navigable);
    }
}

void SiteIsolationManager::remove_all_pages_for_client(WebContentClient& client)
{
    Vector<NonnullRefPtr<WebContentPage>> pages;
    client.for_each_page([&](WebContentPage& page) {
        pages.append(page);
        return IterationDecision::Continue;
    });
    for (auto const& page : pages)
        remove_page(page);
}

String SiteIsolationManager::dump_process_tree(WebContentClient& client, Compositing::PageId page_id) const
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

    builder.appendff("WebContent#{}\n", process_index(client));
    if (auto* page = client.page(page_id))
        dump_frame_tree(page->traversable(), 1);
    return builder.to_string_without_validation();
}

HashMap<pid_t, pid_t> SiteIsolationManager::remote_frame_process_embedders() const
{
    HashMap<pid_t, pid_t> embedders;

    WebContentClient::for_each_client([&](WebContentClient& client) {
        client.for_each_page([&](WebContentPage& page) {
            if (page.displays_tab())
                return IterationDecision::Continue;

            // The process holding the container of a navigable the page hosts embeds the page.
            page.traversable().for_each_in_subtree([&](CanonicalNavigable const& navigable) {
                if (!navigable.has_remote_host() || &navigable.remote_host() != &page)
                    return IterationDecision::Continue;
                embedders.set(client.pid(), navigable.reporting_page()->client().pid());
                return IterationDecision::Break;
            });
            return IterationDecision::Continue;
        });

        return IterationDecision::Continue;
    });

    return embedders;
}

void SiteIsolationManager::set_child_document_host(CanonicalNavigable& navigable, WebContentPage& host)
{
    if (navigable.pending_host_matches(host))
        navigable.clear_pending_host();

    if (navigable.reporting_page().ptr() == &host) {
        if (navigable.has_remote_host())
            transition_child_frame_to_local(navigable);
    } else if (!navigable.has_remote_host() || &navigable.remote_host() != &host) {
        transition_child_frame_to_remote(*navigable.reporting_page(), navigable.id(), host);
    }
}

// A local navigable taking a child's container back starts from a document standing in for the canonical current
// entry's, as the root of an embedded page does.
static Optional<Web::HTML::SessionHistoryEntryDescriptor> current_history_entry_for(CanonicalNavigable& navigable)
{
    auto& traversable = navigable.top_level_traversable();
    auto current_step = traversable.session_history().current_step();
    if (!current_step.has_value())
        return {};
    // NB: The canonical session history can still lack the nested history of a newly created navigable.
    auto const* current_entry = traversable.session_history().get_the_target_history_entry(navigable, *current_step);
    if (!current_entry)
        return {};
    return *current_entry;
}

void SiteIsolationManager::transition_child_frame_to_remote(WebContentPage& parent_page, Web::HTML::CrossProcessId frame_id, NonnullRefPtr<WebContentPage> remote_page)
{
    if (!parent_page.is_open())
        return;
    auto child_frame = parent_page.traversable().top_level_traversable().find(frame_id);
    if (!child_frame.has_value())
        return;

    child_frame->hand_pending_webdriver_commands_to(*remote_page);
    detach_child_frame_host(*child_frame);

    child_frame->set_remote_host(move(remote_page));
    // The page holding the container represents the child from its replicated state, which names the compositor
    // context the host paints it through.
    parent_page.async_stop_hosting_navigable(child_frame->id(), *child_frame->replicated_state());
}

// The child's next document, or none after its host went away, is hosted by the page holding its container.
void SiteIsolationManager::transition_child_frame_to_local(CanonicalNavigable& child_frame)
{
    child_frame.hand_pending_webdriver_commands_to(*child_frame.reporting_page());
    detach_child_frame_host(child_frame);
    auto current_history_entry = current_history_entry_for(child_frame);
    if (!current_history_entry.has_value())
        return;
    child_frame.reporting_page()->async_host_navigable(child_frame.id(), current_history_entry.release_value(), child_frame.top_level_traversable().system_visibility_state());
}

void SiteIsolationManager::detach_child_frame_host(CanonicalNavigable& child_frame)
{
    // The frames of the displaced document, which its host reported, die with it and are not reported destroyed
    // again. The frames of the next document, reported by its host, stay.
    if (child_frame.has_remote_host()) {
        auto const& host = child_frame.remote_host();
        Vector<Web::HTML::CrossProcessId> displaced_frames;
        for (auto const& child : child_frame.children()) {
            if (child->reporting_page().ptr() == &host)
                displaced_frames.append(child->id());
        }
        for (auto frame_id : displaced_frames) {
            if (auto frame = child_frame.top_level_traversable().find(frame_id); frame.has_value())
                remove_child_frame_subtree(*frame);
        }
    }

    child_frame.detach_remote_host();
}

void SiteIsolationManager::remove_child_frame_subtree(CanonicalNavigable& child_frame)
{
    while (!child_frame.children().is_empty())
        remove_child_frame_subtree(*child_frame.children().last());

    if (child_frame.has_remote_host())
        detach_child_frame_host(child_frame);

    child_frame.top_level_traversable().remove(child_frame);
}

}
