/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibIPC/Decoder.h>
#include <LibIPC/Encoder.h>
#include <LibWeb/Bindings/PrincipalHostDefined.h>
#include <LibWeb/HTML/BrowsingContext.h>
#include <LibWeb/HTML/Navigable.h>
#include <LibWeb/HTML/NavigableContainer.h>
#include <LibWeb/HTML/TraversableNavigable.h>
#include <LibWeb/HTML/WindowProxy.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(Navigable);

GC::Ref<Navigable> Navigable::create_remote(JS::Realm& realm, RemoteNavigableDescriptor descriptor, GC::Ptr<Navigable> parent)
{
    auto navigable = realm.heap().allocate<Navigable>();
    auto remote_browsing_context = BrowsingContext::create_remote(Bindings::principal_host_defined_page(realm), descriptor, parent ? parent->active_browsing_context() : nullptr);
    remote_browsing_context->set_remote_navigable(navigable);
    navigable->set_id(move(descriptor.id));
    navigable->set_parent(parent);
    navigable->set_remote_state({
        .target_name = move(descriptor.target_name),
        .active_document_origin = move(descriptor.active_document_origin),
        .active_document_top_level_creation_url = move(descriptor.active_document_top_level_creation_url),
        .active_document_top_level_origin = move(descriptor.active_document_top_level_origin),
        .active_document_is_fully_active = descriptor.active_document_is_fully_active,
        .is_closed = descriptor.is_closed,
        .active_document_child_navigable_count = descriptor.active_document_child_navigable_count,
        .is_traversable = descriptor.is_traversable,
        .is_top_level_traversable = descriptor.is_top_level_traversable,
        .active_browsing_context = remote_browsing_context,
    });
    navigable->ensure_remote_active_window_proxy(realm);
    if (descriptor.is_traversable)
        navigable->set_traversable_navigable(TraversableNavigable::create_remote(navigable));
    return navigable;
}

String Navigable::target_name() const
{
    if (auto const* remote_state = m_state.get_pointer<RemoteNavigableState>())
        return remote_state->target_name;
    return local_target_name();
}

GC::Ptr<WindowProxy> Navigable::active_window_proxy()
{
    if (!m_active_window_proxy && has_local_state())
        set_active_window_proxy(local_active_window_proxy());
    return m_active_window_proxy;
}

GC::Ptr<BrowsingContext> Navigable::active_browsing_context()
{
    if (auto const* remote_state = m_state.get_pointer<RemoteNavigableState>())
        return remote_state->active_browsing_context;
    return local_active_browsing_context();
}

Optional<URL::Origin> Navigable::active_document_origin() const
{
    if (auto const* remote_state = m_state.get_pointer<RemoteNavigableState>())
        return remote_state->active_document_origin;
    return local_active_document_origin();
}

Optional<URL::URL> Navigable::active_document_top_level_creation_url() const
{
    if (auto const* remote_state = m_state.get_pointer<RemoteNavigableState>())
        return remote_state->active_document_top_level_creation_url;
    return local_active_document_top_level_creation_url();
}

Optional<URL::Origin> Navigable::active_document_top_level_origin() const
{
    if (auto const* remote_state = m_state.get_pointer<RemoteNavigableState>())
        return remote_state->active_document_top_level_origin;
    return local_active_document_top_level_origin();
}

bool Navigable::active_document_is_fully_active() const
{
    if (auto const* remote_state = m_state.get_pointer<RemoteNavigableState>())
        return remote_state->active_document_is_fully_active;
    return local_active_document_is_fully_active();
}

bool Navigable::is_closed() const
{
    if (auto const* remote_state = m_state.get_pointer<RemoteNavigableState>())
        return remote_state->is_closed;
    return local_is_closed();
}

size_t Navigable::active_document_child_navigable_count() const
{
    if (auto const* remote_state = m_state.get_pointer<RemoteNavigableState>())
        return remote_state->active_document_child_navigable_count;
    return local_active_document_child_navigable_count();
}

GC::Ptr<Navigable> Navigable::top_level_navigable()
{
    GC::Ptr<Navigable> navigable = this;
    while (navigable->parent())
        navigable = navigable->parent();
    return navigable;
}

GC::Ptr<Navigable const> Navigable::top_level_navigable() const
{
    GC::Ptr<Navigable const> navigable = this;
    while (navigable->parent())
        navigable = navigable->parent();
    return navigable;
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#ancestor-browsing-context
bool Navigable::is_ancestor_of(Navigable const& potential_descendant) const
{
    // 1. Let potentialDescendantDocument be potentialDescendant's active document.
    // 2. If potentialDescendantDocument is not fully active, then return false.
    if (!potential_descendant.active_document_is_fully_active())
        return false;

    // 3. Let ancestorBCs be the list obtained by taking the browsing context of the active document of each member of potentialDescendantDocument's ancestor navigables.
    for (auto ancestor = potential_descendant.parent(); ancestor; ancestor = ancestor->parent()) {
        // 4. If ancestorBCs contains potentialAncestor, then return true.
        if (ancestor.ptr() == this)
            return true;
    }

    // 5. Return false.
    return false;
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#familiar-with
bool Navigable::is_familiar_with(Navigable const& other) const
{
    // A browsing context A is familiar with a second browsing context B if the following algorithm returns true:
    auto const& A = *this;
    auto const& B = other;

    // 1. If A's active document's origin is same origin with B's active document's origin, then return true.
    auto a_origin = A.active_document_origin();
    auto b_origin = B.active_document_origin();
    if (a_origin.has_value() && b_origin.has_value() && a_origin->is_same_origin(*b_origin))
        return true;

    // 2. If A's top-level browsing context is B, then return true.
    auto a_top_level_navigable = A.top_level_navigable();
    if (a_top_level_navigable.ptr() == &B)
        return true;

    // 3. If B is an auxiliary browsing context and A is familiar with B's opener browsing context, then return true.
    if (auto opener = B.opener()) {
        if (A.is_familiar_with(*opener))
            return true;
    }

    // 4. If there exists an ancestor browsing context of B whose active document has the same origin as the active document of A, then return true.
    // NOTE: This includes the case where A is an ancestor browsing context of B.
    if (!B.active_document_is_fully_active())
        return false;

    if (!a_origin.has_value())
        return false;

    for (auto ancestor = B.parent(); ancestor; ancestor = ancestor->parent()) {
        auto ancestor_origin = ancestor->active_document_origin();
        if (ancestor_origin.has_value() && ancestor_origin->is_same_origin(*a_origin))
            return true;
    }

    // 5. Return false.
    return false;
}

RemoteNavigableDescriptor Navigable::remote_descriptor() const
{
    return {
        .id = id(),
        .target_name = target_name(),
        .active_document_origin = active_document_origin(),
        .active_document_top_level_creation_url = active_document_top_level_creation_url(),
        .active_document_top_level_origin = active_document_top_level_origin(),
        .active_document_is_fully_active = active_document_is_fully_active(),
        .is_closed = is_closed(),
        .active_document_child_navigable_count = active_document_child_navigable_count(),
        .is_traversable = is_traversable(),
        .is_top_level_traversable = is_top_level_traversable(),
    };
}

void Navigable::update_remote_descriptor(RemoteNavigableDescriptor descriptor)
{
    VERIFY(!has_local_state());
    VERIFY(descriptor.id == id());

    auto& remote_state = m_state.get<RemoteNavigableState>();
    remote_state.target_name = move(descriptor.target_name);
    remote_state.active_document_origin = move(descriptor.active_document_origin);
    remote_state.active_document_top_level_creation_url = move(descriptor.active_document_top_level_creation_url);
    remote_state.active_document_top_level_origin = move(descriptor.active_document_top_level_origin);
    remote_state.active_document_is_fully_active = descriptor.active_document_is_fully_active;
    remote_state.is_closed = descriptor.is_closed;
    remote_state.active_document_child_navigable_count = descriptor.active_document_child_navigable_count;
    remote_state.is_traversable = descriptor.is_traversable;
    remote_state.is_top_level_traversable = descriptor.is_top_level_traversable;

    if (remote_state.active_browsing_context)
        remote_state.active_browsing_context->update_remote_state(remote_descriptor());

    if (remote_state.is_traversable && !m_traversable)
        set_traversable_navigable(TraversableNavigable::create_remote(*this));
}

LocalNavigable& Navigable::local()
{
    VERIFY(has_local_state());
    return as<LocalNavigable>(*this);
}

LocalNavigable const& Navigable::local() const
{
    VERIFY(has_local_state());
    return as<LocalNavigable>(*this);
}

void Navigable::set_remote_state(RemoteNavigableState state)
{
    m_state = move(state);
    if (m_active_window_proxy)
        m_active_window_proxy->set_remote_navigable(*this);
}

void Navigable::set_local_state()
{
    m_state = LocalNavigableState {};
    set_active_window_proxy(local_active_window_proxy());
}

void Navigable::set_active_window_proxy(GC::Ptr<WindowProxy> window_proxy)
{
    m_active_window_proxy = move(window_proxy);
    if (m_active_window_proxy && !has_local_state())
        m_active_window_proxy->set_remote_navigable(*this);
}

GC::Ref<WindowProxy> Navigable::ensure_remote_active_window_proxy(JS::Realm& realm)
{
    VERIFY(!has_local_state());
    if (!m_active_window_proxy)
        set_active_window_proxy(WindowProxy::create_remote(realm, *this));
    VERIFY(m_active_window_proxy);
    return *m_active_window_proxy;
}

bool Navigable::is_traversable() const
{
    if (auto const* remote_state = m_state.get_pointer<RemoteNavigableState>())
        return remote_state->is_traversable;
    return false;
}

bool Navigable::is_top_level_traversable() const
{
    if (auto const* remote_state = m_state.get_pointer<RemoteNavigableState>())
        return remote_state->is_top_level_traversable;
    return false;
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
    return navigable->m_traversable;
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
    return navigable->m_traversable;
}

void Navigable::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_parent);
    visitor.visit(m_opener);
    visitor.visit(m_container);
    visitor.visit(m_traversable);
    visitor.visit(m_active_window_proxy);
    m_state.visit(
        [](LocalNavigableState&) {},
        [&](RemoteNavigableState& remote_state) {
            visitor.visit(remote_state.active_browsing_context);
        });
}

}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::HTML::RemoteNavigableDescriptor const& descriptor)
{
    TRY(encoder.encode(descriptor.id));
    TRY(encoder.encode(descriptor.target_name));
    TRY(encoder.encode(descriptor.active_document_origin));
    TRY(encoder.encode(descriptor.active_document_top_level_creation_url));
    TRY(encoder.encode(descriptor.active_document_top_level_origin));
    TRY(encoder.encode(descriptor.active_document_is_fully_active));
    TRY(encoder.encode(descriptor.is_closed));
    TRY(encoder.encode(descriptor.active_document_child_navigable_count));
    TRY(encoder.encode(descriptor.is_traversable));
    TRY(encoder.encode(descriptor.is_top_level_traversable));
    return {};
}

template<>
ErrorOr<Web::HTML::RemoteNavigableDescriptor> IPC::decode(Decoder& decoder)
{
    auto id = TRY(decoder.decode<String>());
    auto target_name = TRY(decoder.decode<String>());
    auto active_document_origin = TRY(decoder.decode<Optional<URL::Origin>>());
    auto active_document_top_level_creation_url = TRY(decoder.decode<Optional<URL::URL>>());
    auto active_document_top_level_origin = TRY(decoder.decode<Optional<URL::Origin>>());
    auto active_document_is_fully_active = TRY(decoder.decode<bool>());
    auto is_closed = TRY(decoder.decode<bool>());
    auto active_document_child_navigable_count = TRY(decoder.decode<size_t>());
    auto is_traversable = TRY(decoder.decode<bool>());
    auto is_top_level_traversable = TRY(decoder.decode<bool>());

    return Web::HTML::RemoteNavigableDescriptor {
        .id = move(id),
        .target_name = move(target_name),
        .active_document_origin = move(active_document_origin),
        .active_document_top_level_creation_url = move(active_document_top_level_creation_url),
        .active_document_top_level_origin = move(active_document_top_level_origin),
        .active_document_is_fully_active = active_document_is_fully_active,
        .is_closed = is_closed,
        .active_document_child_navigable_count = active_document_child_navigable_count,
        .is_traversable = is_traversable,
        .is_top_level_traversable = is_top_level_traversable,
    };
}
