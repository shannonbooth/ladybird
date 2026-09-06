/*
 * Copyright (c) 2026-present, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Badge.h>
#include <AK/Function.h>
#include <AK/IterationDecision.h>
#include <AK/Utf16String.h>
#include <LibGC/Function.h>
#include <LibGC/Ptr.h>
#include <LibJS/Heap/Cell.h>
#include <LibURL/URL.h>
#include <LibWeb/ContentSecurityPolicy/Directives/Directive.h>
#include <LibWeb/Export.h>
#include <LibWeb/Forward.h>
#include <LibWeb/HTML/ApplyHistoryStep.h>
#include <LibWeb/HTML/CrossOrigin/OpenerPolicy.h>
#include <LibWeb/HTML/CrossProcessId.h>
#include <LibWeb/HTML/NavigateParams.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/document-sequences.html#navigable
class WEB_API Navigable : public JS::Cell {
    GC_CELL(Navigable, JS::Cell);

public:
    virtual ~Navigable() override;

    // https://html.spec.whatwg.org/multipage/document-sequences.html#nav-id
    CrossProcessId id() const { return m_id; }

    GC::Ptr<Navigable> parent() const { return m_parent; }

    // https://html.spec.whatwg.org/multipage/document-sequences.html#nav-container
    GC::Ptr<NavigableContainer> container() const;
    void set_container(Badge<NavigableContainer>, GC::Ptr<NavigableContainer> container) { m_container = container; }
    // https://html.spec.whatwg.org/multipage/document-sequences.html#nav-container-document
    GC::Ptr<DOM::Document> container_document() const;

    Page& page() { return m_page; }
    Page const& page() const { return m_page; }

    bool is_ancestor_of(Navigable const&) const;

    // The navigable with the given id among this navigable's inclusive descendants, or null.
    GC::Ptr<Navigable> find(CrossProcessId);

    bool has_been_destroyed() const { return m_has_been_destroyed; }
    virtual void set_has_been_destroyed();
    void report_child_frame_destroyed();
    // AD-HOC: Child removal unloads documents before running the remaining destroy-a-child-navigable steps.
    void unload_child_navigable_before_destruction(GC::Ref<GC::Function<void()>> after_all_unloads);
    void continue_child_navigable_destruction(UnloadDisplayedDocument);

    virtual GC::Ptr<WindowProxy> active_window_proxy() = 0;
    virtual Utf16String const& target_name() const = 0;
    virtual bool is_traversable() const { return false; }
    GC::Ref<Navigable> traversable_navigable();
    GC::Ref<Navigable> top_level_traversable();
    virtual bool is_top_level_traversable() const { return false; }
    virtual Optional<URL::URL> active_document_url() const = 0;
    virtual Optional<URL::Origin> active_document_origin() const = 0;
    virtual bool active_document_is_fully_active() const = 0;
    virtual bool active_document_is(DOM::Document const&) const = 0;

    virtual Optional<URL::URL> active_document_top_level_creation_url() const = 0;
    virtual Optional<URL::Origin> active_document_top_level_origin() const = 0;
    virtual bool active_document_has_cross_site_ancestor() const = 0;
    virtual OpenerPolicy const& active_document_opener_policy() const = 0;

    // Whether the navigable's container is in its node document's tree rather than a shadow tree, which makes the
    // navigable a document-tree child navigable of that document.
    virtual bool container_is_in_document_tree() const = 0;

    virtual bool has_session_history_entry_and_ready_for_navigation() const = 0;
    virtual bool delays_the_load_event_of_its_container() const = 0;

    WebIDL::ExceptionOr<void> navigate(NavigateParams);

    bool allowed_by_sandboxing_to_navigate(Navigable const& target, SourceSnapshotParams const&) const;

protected:
    explicit Navigable(GC::Ref<Page>);
    void set_id(CrossProcessId id) { m_id = id; }
    void set_parent(GC::Ptr<Navigable> parent) { m_parent = parent; }

    virtual WebIDL::ExceptionOr<void> continue_navigation_in_active_document_agent(PreparedNavigation) = 0;

    virtual void for_each_child_navigable(Function<IterationDecision(Navigable&)> const&) = 0;

    // What becomes of the displayed document when the UI process continues a child's destruction in this process.
    virtual void unload_for_child_navigable_destruction(UnloadDisplayedDocument) = 0;

    virtual void visit_edges(Cell::Visitor&) override;

private:
    CrossProcessId m_id;

    // https://html.spec.whatwg.org/multipage/document-sequences.html#nav-parent
    GC::Ptr<Navigable> m_parent;

    // Implied link between navigable and its container.
    GC::Ptr<NavigableContainer> m_container;

    // The page whose navigable tree this navigable belongs to.
    GC::Ref<Page> m_page;

    bool m_has_been_destroyed { false };
    bool m_child_frame_destruction_reported { false };

    // The destroy-a-child-navigable continuation parked while the UI process unloads this navigable's document tree.
    GC::Ptr<GC::Function<void()>> m_pending_child_navigable_unload;
};

}
