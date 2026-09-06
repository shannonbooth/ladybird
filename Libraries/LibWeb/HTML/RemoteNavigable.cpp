/*
 * Copyright (c) 2026-present, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGC/Heap.h>
#include <LibWeb/HTML/PreparedNavigationDescriptor.h>
#include <LibWeb/HTML/RemoteNavigable.h>
#include <LibWeb/Page/Page.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(RemoteNavigable);

GC::Ref<RemoteNavigable> RemoteNavigable::create(GC::Ref<Page> page, CrossProcessId id, GC::Ptr<Navigable> parent, ReplicatedNavigableState replicated_state)
{
    return GC::Heap::the().allocate<RemoteNavigable>(page, id, parent, move(replicated_state));
}

RemoteNavigable::RemoteNavigable(GC::Ref<Page> page, CrossProcessId id, GC::Ptr<Navigable> parent, ReplicatedNavigableState replicated_state)
    : m_page(page)
    , m_replicated_state(move(replicated_state))
{
    set_id(id);
    set_parent(parent);
}

RemoteNavigable::~RemoteNavigable() = default;

void RemoteNavigable::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_page);
}

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

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#navigate
WebIDL::ExceptionOr<void> RemoteNavigable::continue_navigation_in_active_document_agent(PreparedNavigation navigation)
{
    // 8. If the surrounding agent is equal to navigable's active document's relevant agent, then continue these
    //    steps. Otherwise, queue a global task on the navigation and traversal task source given navigable's active
    //    window to continue these steps.
    // NB: The active window lives in the process hosting the active document, so the task is a request to the UI
    //     process, which forwards it to that process.
    m_page->client().request_navigation_of_remote_navigable(*this, create_prepared_navigation_descriptor(navigation));
    return {};
}

}
