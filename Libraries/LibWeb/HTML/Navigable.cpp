/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/HTML/Navigable.h>
#include <LibWeb/HTML/NavigableContainer.h>
#include <LibWeb/HTML/TraversableNavigable.h>
#include <LibWeb/HTML/WindowProxy.h>

namespace Web::HTML {

String Navigable::target_name() const
{
    if (auto const* remote_state = m_state.get_pointer<RemoteNavigableState>())
        return remote_state->target_name;
    return local_target_name();
}

GC::Ptr<WindowProxy> Navigable::active_window_proxy()
{
    if (auto const* remote_state = m_state.get_pointer<RemoteNavigableState>())
        return remote_state->active_window_proxy;
    return local_active_window_proxy();
}

void Navigable::set_remote_state(RemoteNavigableState state)
{
    m_state = move(state);
}

void Navigable::set_local_state()
{
    m_state = LocalNavigableState {};
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#nav-traversable
GC::Ptr<TraversableNavigable> Navigable::traversable_navigable() const
{
    // 1. Let navigable be inputNavigable.
    GC::Ptr<Navigable> navigable = const_cast<Navigable*>(this);

    // 2. While navigable is not a traversable navigable, set navigable to navigable's parent.
    while (navigable && !navigable->is_traversable())
        navigable = navigable->parent();

    // 3. Return navigable.
    if (!navigable)
        return nullptr;
    return as<TraversableNavigable>(*navigable);
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#nav-top
GC::Ptr<TraversableNavigable> Navigable::top_level_traversable()
{
    // 1. Let navigable be inputNavigable.
    GC::Ptr<Navigable> navigable = this;

    // 2. While navigable's parent is not null, set navigable to navigable's parent.
    while (navigable->parent())
        navigable = navigable->parent();

    // 3. Return navigable.
    return as<TraversableNavigable>(*navigable);
}

void Navigable::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_parent);
    visitor.visit(m_container);
    m_state.visit(
        [](LocalNavigableState&) {},
        [&](RemoteNavigableState& remote_state) {
            visitor.visit(remote_state.active_window_proxy);
        });
}

}
