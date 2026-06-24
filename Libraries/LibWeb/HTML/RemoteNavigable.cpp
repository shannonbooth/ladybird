/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/HTML/RemoteNavigable.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(RemoteNavigable);

RemoteNavigable::RemoteNavigable(GC::Ptr<Navigable> parent)
    : m_parent(parent)
{
}

void RemoteNavigable::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_parent);
}

}
