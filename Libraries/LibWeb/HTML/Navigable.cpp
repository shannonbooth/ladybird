/*
 * Copyright (c) 2026, Ladybird contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/HTML/Navigable.h>
#include <LibWeb/HTML/WindowProxy.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(Navigable);

Navigable::~Navigable() = default;

void Navigable::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_parent);
    visitor.visit(m_child_navigables);
    visitor.visit(m_active_window_proxy);
}

void Navigable::set_parent(GC::Ptr<Navigable> parent)
{
    if (m_parent == parent)
        return;

    if (m_parent)
        m_parent->remove_child_navigable(*this);

    m_parent = parent;

    if (m_parent)
        m_parent->append_child_navigable(*this);
}

Vector<GC::Root<Navigable>> Navigable::child_navigables() const
{
    Vector<GC::Root<Navigable>> children;
    children.ensure_capacity(m_child_navigables.size());
    for (auto const& child : m_child_navigables)
        children.unchecked_append(child);
    return children;
}

void Navigable::append_child_navigable(GC::Ref<Navigable> child)
{
    if (m_child_navigables.contains_slow(child))
        return;
    m_child_navigables.append(child);
}

void Navigable::remove_child_navigable(Navigable& child)
{
    m_child_navigables.remove_first_matching([&](auto const& existing_child) {
        return existing_child.ptr() == &child;
    });
}

}
