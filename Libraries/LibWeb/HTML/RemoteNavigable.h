/*
 * Copyright (c) 2026-present, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/Utf16FlyString.h>
#include <AK/Vector.h>
#include <LibGC/Root.h>
#include <LibWeb/HTML/Navigable.h>
#include <LibWeb/HTML/ReplicatedNavigableState.h>
#include <LibWeb/HTML/Window.h>

namespace Web::HTML {

// A navigable whose active document is hosted by another WebContent process. It answers what the local tree asks of
// it from the state the UI process replicates from the canonical tree. Anything that needs the document is a request
// to the UI process, and VERIFYs until that request exists.
class WEB_API RemoteNavigable final : public Navigable {
    GC_CELL(RemoteNavigable, Navigable);
    GC_DECLARE_ALLOCATOR(RemoteNavigable);

public:
    static GC::Ref<RemoteNavigable> create(GC::Ref<Page>, CrossProcessId, GC::Ptr<Navigable> parent, ReplicatedNavigableState);
    virtual ~RemoteNavigable() override;

    ReplicatedNavigableState const& replicated_state() const { return m_replicated_state; }
    void set_replicated_state(ReplicatedNavigableState state) { m_replicated_state = move(state); }

    void append_child(GC::Ref<Navigable>);
    void remove_child(Navigable&);

    // https://html.spec.whatwg.org/multipage/document-sequences.html#document-tree-child-navigable
    Vector<GC::Root<Navigable>> document_tree_child_navigables();
    // https://html.spec.whatwg.org/multipage/nav-history-apis.html#document-tree-child-navigable-target-name-property-set
    OrderedHashMap<Utf16FlyString, GC::Ref<Navigable>> document_tree_child_navigable_target_name_property_set();
    // https://html.spec.whatwg.org/multipage/nav-history-apis.html#dom-window-closed
    bool is_closed() const;
    // https://html.spec.whatwg.org/multipage/nav-history-apis.html#dom-location
    GC::Ref<Location> location();
    // https://html.spec.whatwg.org/multipage/web-messaging.html#dom-window-postmessage-options
    WebIDL::ExceptionOr<void> post_message(JS::Realm&, JS::Value message, Window::PostMessageOptions const&);

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
    virtual Vector<GC::Root<Navigable>> active_document_inclusive_descendant_navigables() override;
    virtual Optional<URL::URL> active_document_top_level_creation_url() const override { return m_replicated_state.top_level_creation_url; }
    virtual Optional<URL::Origin> active_document_top_level_origin() const override { return m_replicated_state.top_level_origin; }
    virtual bool active_document_has_cross_site_ancestor() const override { return m_replicated_state.has_cross_site_ancestor; }
    virtual OpenerPolicy const& active_document_opener_policy() const override { return m_replicated_state.opener_policy; }
    virtual bool container_is_in_document_tree() const override { return m_replicated_state.container_is_in_document_tree; }

    virtual bool has_session_history_entry_and_ready_for_navigation() const override;
    virtual bool delays_the_load_event_of_its_container() const override;

private:
    RemoteNavigable(GC::Ref<Page>, CrossProcessId, GC::Ptr<Navigable> parent, ReplicatedNavigableState);

    virtual void visit_edges(Cell::Visitor&) override;

    virtual WebIDL::ExceptionOr<void> continue_navigation_in_active_document_agent(PreparedNavigation) override;
    virtual void for_each_child_navigable(Function<IterationDecision(Navigable&)> const&) override;
    virtual void unload_for_child_navigable_destruction(UnloadDisplayedDocument) override;

    ReplicatedNavigableState m_replicated_state;

    // The navigable's child navigables, in the order the UI process learned of their creation.
    Vector<GC::Ref<Navigable>> m_children;

    // https://html.spec.whatwg.org/multipage/document-sequences.html#nav-wp
    GC::Ptr<WindowProxy> m_window_proxy;
    GC::Ptr<Location> m_location;
};

}
