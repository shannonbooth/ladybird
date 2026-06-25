/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Variant.h>
#include <AK/String.h>
#include <LibGC/Ptr.h>
#include <LibJS/Heap/Cell.h>
#include <LibWeb/Export.h>
#include <LibWeb/Forward.h>

namespace Web::HTML {

struct LocalNavigableState {
};

struct RemoteNavigableState {
    String target_name;
    GC::Ptr<WindowProxy> active_window_proxy;
};

class WEB_API Navigable : public JS::Cell {
    GC_CELL(Navigable, JS::Cell);

public:
    String const& id() const { return m_id; }
    String target_name() const;
    GC::Ptr<Navigable> parent() const { return m_parent; }
    GC::Ptr<NavigableContainer> container() const { return m_container; }
    GC::Ptr<WindowProxy> active_window_proxy();

    void set_remote_state(RemoteNavigableState);
    void set_local_state();
    bool has_local_state() const { return m_state.has<LocalNavigableState>(); }

    GC::Ptr<TraversableNavigable> traversable_navigable() const;
    GC::Ptr<TraversableNavigable> top_level_traversable();

    virtual bool is_traversable() const { return false; }
    virtual bool is_top_level_traversable() const { return false; }

protected:
    void set_id(String id) { m_id = move(id); }
    void set_parent(GC::Ptr<Navigable> parent) { m_parent = parent; }
    void set_container(GC::Ptr<NavigableContainer> container) { m_container = container; }

    virtual String local_target_name() const { return {}; }
    virtual GC::Ptr<WindowProxy> local_active_window_proxy() { return nullptr; }

    virtual void visit_edges(Visitor&) override;

private:
    String m_id;
    GC::Ptr<Navigable> m_parent;
    GC::Ptr<NavigableContainer> m_container;
    Variant<LocalNavigableState, RemoteNavigableState> m_state { LocalNavigableState {} };

    friend class NavigableContainer;
};

}
