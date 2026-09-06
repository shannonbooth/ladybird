/*
 * Copyright (c) 2026-present, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/Vector.h>
#include <LibWeb/HTML/Navigable.h>
#include <LibWeb/HTML/ReplicatedNavigableState.h>

namespace Web::HTML {

// A navigable whose active document is hosted by another WebContent process. It answers what the local tree asks of
// it from the state the UI process replicated when this page was created; nothing updates that state yet. Anything
// that needs the document will be a request to the UI process, and VERIFYs until those requests exist.
class WEB_API RemoteNavigable final : public Navigable {
    GC_CELL(RemoteNavigable, Navigable);
    GC_DECLARE_ALLOCATOR(RemoteNavigable);

public:
    static GC::Ref<RemoteNavigable> create(GC::Ref<Page>, CrossProcessId, GC::Ptr<Navigable> parent, ReplicatedNavigableState);
    virtual ~RemoteNavigable() override;

    Page& page() { return m_page; }

    ReplicatedNavigableState const& replicated_state() const { return m_replicated_state; }
    void set_replicated_state(ReplicatedNavigableState state) { m_replicated_state = move(state); }

    void append_child(GC::Ref<Navigable>);
    void remove_child(Navigable&);

    // A remote navigable is never destroyed from this process: the UI process discards the page hosting its children instead.
    virtual bool has_been_destroyed() const override { return false; }

    virtual GC::Ptr<WindowProxy> active_window_proxy() override;
    virtual Utf16String const& target_name() const override { return m_replicated_state.target_name; }

    // Ladybird has no nested traversables, so a remote navigable is a traversable exactly when it is the root.
    virtual bool is_traversable() const override { return parent() == nullptr; }
    virtual bool is_top_level_traversable() const override { return parent() == nullptr; }

    virtual Optional<URL::URL> active_document_url() const override { return m_replicated_state.active_document_url; }
    virtual Optional<URL::Origin> active_document_origin() const override { return m_replicated_state.active_document_origin; }
    virtual bool active_document_is_fully_active() const override { return m_replicated_state.active_document_is_fully_active; }
    // The active document lives in the process hosting it, so no document of this process is it.
    virtual bool active_document_is(DOM::Document const&) const override { return false; }
    virtual Optional<URL::URL> active_document_top_level_creation_url() const override { return m_replicated_state.top_level_creation_url; }
    virtual Optional<URL::Origin> active_document_top_level_origin() const override { return m_replicated_state.top_level_origin; }
    virtual bool active_document_has_cross_site_ancestor() const override { return m_replicated_state.has_cross_site_ancestor; }
    virtual OpenerPolicy const& active_document_opener_policy() const override { return m_replicated_state.opener_policy; }

    virtual bool has_session_history_entry_and_ready_for_navigation() const override;
    virtual bool delays_the_load_event_of_its_container() const override;

private:
    RemoteNavigable(GC::Ref<Page>, CrossProcessId, GC::Ptr<Navigable> parent, ReplicatedNavigableState);

    virtual void visit_edges(Cell::Visitor&) override;

    virtual WebIDL::ExceptionOr<void> continue_navigation_in_active_document_agent(PreparedNavigation) override;
    virtual void for_each_child_navigable(Function<IterationDecision(Navigable&)> const&) override;

    // The page whose navigable graph this node belongs to, and whose client carries requests to the UI process.
    GC::Ref<Page> m_page;
    ReplicatedNavigableState m_replicated_state;

    // The navigable's child navigables, in the order the UI process learned of their creation.
    Vector<GC::Ref<Navigable>> m_children;
};

}
