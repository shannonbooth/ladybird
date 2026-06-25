/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibIPC/Decoder.h>
#include <LibIPC/Encoder.h>
#include <LibWeb/HTML/Navigable.h>
#include <LibWeb/HTML/NavigableContainer.h>
#include <LibWeb/HTML/TraversableNavigable.h>
#include <LibWeb/HTML/WindowProxy.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(Navigable);

GC::Ref<Navigable> Navigable::create_remote(JS::Realm& realm, RemoteNavigableDescriptor descriptor, GC::Ptr<Navigable> parent)
{
    auto navigable = realm.heap().allocate<Navigable>();
    navigable->set_id(move(descriptor.id));
    navigable->set_parent(parent);
    navigable->set_remote_state({
        .target_name = move(descriptor.target_name),
        .active_document_origin = move(descriptor.active_document_origin),
        .active_document_top_level_creation_url = move(descriptor.active_document_top_level_creation_url),
        .active_document_top_level_origin = move(descriptor.active_document_top_level_origin),
        .is_traversable = descriptor.is_traversable,
        .is_top_level_traversable = descriptor.is_top_level_traversable,
        .active_window_proxy = nullptr,
    });
    navigable->m_state.get<RemoteNavigableState>().active_window_proxy = WindowProxy::create_remote(realm, navigable);
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
    if (auto const* remote_state = m_state.get_pointer<RemoteNavigableState>())
        return remote_state->active_window_proxy;
    return local_active_window_proxy();
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

RemoteNavigableDescriptor Navigable::remote_descriptor() const
{
    return {
        .id = id(),
        .target_name = target_name(),
        .active_document_origin = active_document_origin(),
        .active_document_top_level_creation_url = active_document_top_level_creation_url(),
        .active_document_top_level_origin = active_document_top_level_origin(),
        .is_traversable = is_traversable(),
        .is_top_level_traversable = is_top_level_traversable(),
    };
}

void Navigable::set_remote_state(RemoteNavigableState state)
{
    m_state = move(state);
}

void Navigable::set_local_state()
{
    m_state = LocalNavigableState {};
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
    return as<TraversableNavigable>(*navigable);
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#nav-top
GC::Ptr<Navigable> Navigable::top_level_traversable()
{
    // 1. Let navigable be inputNavigable.
    GC::Ptr<Navigable> navigable = this;

    // 2. While navigable's parent is not null, set navigable to navigable's parent.
    while (navigable->parent())
        navigable = navigable->parent();

    // 3. Return navigable.
    return navigable;
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

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::HTML::RemoteNavigableDescriptor const& descriptor)
{
    TRY(encoder.encode(descriptor.id));
    TRY(encoder.encode(descriptor.target_name));
    TRY(encoder.encode(descriptor.active_document_origin));
    TRY(encoder.encode(descriptor.active_document_top_level_creation_url));
    TRY(encoder.encode(descriptor.active_document_top_level_origin));
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
    auto is_traversable = TRY(decoder.decode<bool>());
    auto is_top_level_traversable = TRY(decoder.decode<bool>());

    return Web::HTML::RemoteNavigableDescriptor {
        .id = move(id),
        .target_name = move(target_name),
        .active_document_origin = move(active_document_origin),
        .active_document_top_level_creation_url = move(active_document_top_level_creation_url),
        .active_document_top_level_origin = move(active_document_top_level_origin),
        .is_traversable = is_traversable,
        .is_top_level_traversable = is_top_level_traversable,
    };
}
