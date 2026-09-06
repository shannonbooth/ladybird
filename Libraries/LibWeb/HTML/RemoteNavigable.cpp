/*
 * Copyright (c) 2026-present, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGC/Heap.h>
#include <LibWeb/HTML/RemoteNavigable.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(RemoteNavigable);

GC::Ref<RemoteNavigable> RemoteNavigable::create(CrossProcessId id, GC::Ptr<Navigable> parent, ReplicatedNavigableState replicated_state)
{
    return GC::Heap::the().allocate<RemoteNavigable>(id, parent, move(replicated_state));
}

RemoteNavigable::RemoteNavigable(CrossProcessId id, GC::Ptr<Navigable> parent, ReplicatedNavigableState replicated_state)
    : m_replicated_state(move(replicated_state))
{
    set_id(id);
    set_parent(parent);
}

RemoteNavigable::~RemoteNavigable() = default;

GC::Ptr<WindowProxy> RemoteNavigable::active_window_proxy()
{
    // A WindowProxy that targets a remote navigable arrives with the cross-origin window support. Until then, no
    // path may reach the window of a remote navigable.
    VERIFY_NOT_REACHED();
}

bool RemoteNavigable::has_session_history_entry_and_ready_for_navigation() const
{
    // Only a navigable container asks this of its content navigable, and no remote navigable has a container in this
    // process yet.
    VERIFY_NOT_REACHED();
}

bool RemoteNavigable::delays_the_load_event_of_its_container() const
{
    VERIFY_NOT_REACHED();
}

WebIDL::ExceptionOr<void> RemoteNavigable::continue_navigation_in_active_document_agent(PreparedNavigation)
{
    // Navigating a remote navigable is a request to the process hosting its document, routed by the UI process.
    // Nothing sends that request yet.
    VERIFY_NOT_REACHED();
}

}
