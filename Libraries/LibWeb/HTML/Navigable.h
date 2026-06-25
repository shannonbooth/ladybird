/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/Variant.h>
#include <AK/String.h>
#include <LibGC/Ptr.h>
#include <LibIPC/Forward.h>
#include <LibJS/Heap/Cell.h>
#include <LibURL/Origin.h>
#include <LibURL/URL.h>
#include <LibWeb/Export.h>
#include <LibWeb/Forward.h>

namespace Web::HTML {

struct LocalNavigableState {
};

struct RemoteNavigableState {
    String target_name;
    Optional<URL::Origin> active_document_origin;
    Optional<URL::URL> active_document_top_level_creation_url;
    Optional<URL::Origin> active_document_top_level_origin;
    bool is_traversable { false };
    bool is_top_level_traversable { false };
    GC::Ptr<WindowProxy> active_window_proxy;
};

struct RemoteNavigableDescriptor {
    String id;
    String target_name;
    Optional<URL::Origin> active_document_origin;
    Optional<URL::URL> active_document_top_level_creation_url;
    Optional<URL::Origin> active_document_top_level_origin;
    bool is_traversable { false };
    bool is_top_level_traversable { false };
};

class WEB_API Navigable : public JS::Cell {
    GC_CELL(Navigable, JS::Cell);
    GC_DECLARE_ALLOCATOR(Navigable);

public:
    static GC::Ref<Navigable> create_remote(JS::Realm&, RemoteNavigableDescriptor, GC::Ptr<Navigable> parent);

    String const& id() const { return m_id; }
    void set_id(String id) { m_id = move(id); }

    String target_name() const;
    GC::Ptr<Navigable> parent() const { return m_parent; }
    void set_parent(GC::Ptr<Navigable> parent) { m_parent = parent; }

    GC::Ptr<NavigableContainer> container() const { return m_container; }
    GC::Ptr<WindowProxy> active_window_proxy();
    Optional<URL::Origin> active_document_origin() const;
    Optional<URL::URL> active_document_top_level_creation_url() const;
    Optional<URL::Origin> active_document_top_level_origin() const;
    RemoteNavigableDescriptor remote_descriptor() const;

    void set_remote_state(RemoteNavigableState);
    void set_local_state();
    bool has_local_state() const { return m_state.has<LocalNavigableState>(); }

    GC::Ptr<TraversableNavigable> traversable_navigable() const;
    GC::Ptr<Navigable> top_level_traversable();

    virtual bool is_traversable() const;
    virtual bool is_top_level_traversable() const;

protected:
    Navigable() = default;

    void set_container(GC::Ptr<NavigableContainer> container) { m_container = container; }

    virtual String local_target_name() const { return {}; }
    virtual GC::Ptr<WindowProxy> local_active_window_proxy() { return nullptr; }
    virtual Optional<URL::Origin> local_active_document_origin() const { return {}; }
    virtual Optional<URL::URL> local_active_document_top_level_creation_url() const { return {}; }
    virtual Optional<URL::Origin> local_active_document_top_level_origin() const { return {}; }

    virtual void visit_edges(Visitor&) override;

private:
    String m_id;
    GC::Ptr<Navigable> m_parent;
    GC::Ptr<NavigableContainer> m_container;
    Variant<LocalNavigableState, RemoteNavigableState> m_state { LocalNavigableState {} };

    friend class NavigableContainer;
};

}

namespace IPC {

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::HTML::RemoteNavigableDescriptor const&);

template<>
WEB_API ErrorOr<Web::HTML::RemoteNavigableDescriptor> decode(Decoder&);

}
