/*
 * Copyright (c) 2026-present, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGC/Heap.h>
#include <LibWeb/HTML/LocalNavigable.h>
#include <LibWeb/HTML/Location.h>
#include <LibWeb/HTML/PostedMessageDescriptor.h>
#include <LibWeb/HTML/PreparedNavigationDescriptor.h>
#include <LibWeb/HTML/RemoteNavigable.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/HTML/WindowProxy.h>
#include <LibWeb/Page/Page.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(RemoteNavigable);

GC::Ref<RemoteNavigable> RemoteNavigable::create(GC::Ref<Page> page, CrossProcessId id, GC::Ptr<Navigable> parent, ReplicatedNavigableState replicated_state)
{
    return GC::Heap::the().allocate<RemoteNavigable>(page, id, parent, move(replicated_state));
}

RemoteNavigable::RemoteNavigable(GC::Ref<Page> page, CrossProcessId id, GC::Ptr<Navigable> parent, ReplicatedNavigableState replicated_state)
    : Navigable(page)
    , m_replicated_state(move(replicated_state))
{
    set_id(id);
    set_parent(parent);
}

RemoteNavigable::~RemoteNavigable() = default;

void RemoteNavigable::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_children);
    visitor.visit(m_window_proxy);
    visitor.visit(m_location);
}

GC::Ref<Location> RemoteNavigable::location()
{
    // The Window object's location getter steps are to return this's Location object.
    if (!m_location)
        m_location = GC::Heap::the().allocate<Location>(*this);
    return *m_location;
}

void RemoteNavigable::append_child(GC::Ref<Navigable> child)
{
    VERIFY(child->parent().ptr() == this);
    m_children.append(child);
}

void RemoteNavigable::remove_child(Navigable& child)
{
    auto removed = m_children.remove_first_matching([&](auto const& existing_child) { return existing_child.ptr() == &child; });
    VERIFY(removed);
}

void RemoteNavigable::for_each_child_navigable(Function<IterationDecision(Navigable&)> const& callback)
{
    for (auto& child : m_children) {
        if (callback(*child) == IterationDecision::Break)
            return;
    }
}

GC::Ptr<WindowProxy> RemoteNavigable::active_window_proxy()
{
    // The WindowProxy of a navigable hosted by another process lives in the realm of this page's root document and
    // answers every access on the cross-origin path.
    if (!m_window_proxy) {
        auto window = page().local_root_navigable()->active_window();
        VERIFY(window);
        m_window_proxy = WindowProxy::create_for_remote_navigable(relevant_realm(*window), *this);
    }
    return m_window_proxy;
}

Vector<GC::Root<Navigable>> RemoteNavigable::active_document_inclusive_descendant_navigables()
{
    // The navigable's subtree as the UI process replicates it: itself, then each child's inclusive descendants.
    Vector<GC::Root<Navigable>> navigables;
    navigables.append(*this);
    for (auto& child : m_children)
        navigables.extend(child->active_document_inclusive_descendant_navigables());
    return navigables;
}

Vector<GC::Root<Navigable>> RemoteNavigable::document_tree_child_navigables()
{
    // NB: The children are in the order the UI process learned of their creation, not in tree order.
    Vector<GC::Root<Navigable>> navigables;
    for (auto& child : m_children) {
        if (child->container_is_in_document_tree())
            navigables.append(*child);
    }
    return navigables;
}

OrderedHashMap<Utf16FlyString, GC::Ref<Navigable>> RemoteNavigable::document_tree_child_navigable_target_name_property_set()
{
    // 1. Let children be the document-tree child navigables of window's associated Document.
    auto children = document_tree_child_navigables();

    // 2. Let firstNamedChildren be an empty ordered set.
    OrderedHashMap<Utf16FlyString, GC::Ref<Navigable>> first_named_children;

    // 3. For each navigable of children:
    for (auto const& navigable : children) {
        // 1. Let name be navigable's target name.
        // 2. If name is the empty string, then continue.
        auto const& target_name = navigable->target_name();
        if (target_name.is_empty())
            continue;

        auto name = Utf16FlyString::from_utf16(target_name.utf16_view());

        // 3. If firstNamedChildren contains a navigable whose target name is name, then continue.
        if (first_named_children.contains(name))
            continue;

        // 4. Append navigable to firstNamedChildren.
        (void)first_named_children.set(name, *navigable);
    }

    // 4. Let names be an empty ordered set.
    OrderedHashMap<Utf16FlyString, GC::Ref<Navigable>> names;

    // 5. For each navigable of firstNamedChildren:
    for (auto const& [name, navigable] : first_named_children) {
        // 1. Let name be navigable's target name.
        // 2. If navigable's active document's origin is same origin with window's relevant settings object's origin, then append name to names.
        auto origin = navigable->active_document_origin();
        if (origin.has_value() && origin->is_same_origin(m_replicated_state.active_document_origin))
            names.set(name, navigable);
    }

    // 6. Return names.
    return names;
}

bool RemoteNavigable::is_closed() const
{
    // The closed getter steps are to return true if this's browsing context is null or its is closing is true; otherwise false.
    // NB: A navigable the UI process removed from this graph has no browsing context here anymore.
    return has_been_destroyed() || m_replicated_state.is_closing;
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

void RemoteNavigable::unload_for_child_navigable_destruction(UnloadDisplayedDocument)
{
    // Only a navigable container's content navigable is destroyed this way, and no remote navigable has a container in
    // this process yet.
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
    page().client().request_navigation_of_remote_navigable(*this, create_prepared_navigation_descriptor(navigation));
    return {};
}

// https://html.spec.whatwg.org/multipage/web-messaging.html#window-post-message-steps
WebIDL::ExceptionOr<void> RemoteNavigable::post_message(JS::Realm& realm, JS::Value message, Window::PostMessageOptions const& options)
{
    // 1. Let targetRealm be targetWindow's realm.
    // NB: Taken from targetWindow when the task delivers the message in its process.

    // 2-7.
    auto prepared = TRY(Window::prepare_post_message(realm, message, options));

    // 8. Queue a global task on the posted message task source given targetWindow to run the following steps:
    // NB: targetWindow lives in the process hosting this navigable's document, so the task is a request to the UI
    //     process, which forwards it there. That process represents the source's navigable, which names the source.
    auto source_navigable = prepared.source->window()->navigable();
    VERIFY(source_navigable);
    page().client().request_post_message_to_remote_navigable(*this, {
                                                                        .serialize_with_transfer_result = move(prepared.serialize_with_transfer_result),
                                                                        .target_origin = move(prepared.target_origin),
                                                                        .source_origin = move(prepared.source_origin),
                                                                        .source_navigable_id = source_navigable->id(),
                                                                    });
    return {};
}

}
