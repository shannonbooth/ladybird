/*
 * Copyright (c) 2022, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Forward.h>
#include <AK/Optional.h>
#include <AK/Variant.h>
#include <LibGC/Ptr.h>
#include <LibJS/Forward.h>
#include <LibJS/Runtime/Object.h>
#include <LibWeb/DOM/EventTarget.h>
#include <LibWeb/Export.h>
#include <LibWeb/Forward.h>
#include <LibWeb/HTML/CrossOrigin/CrossOriginPropertyDescriptorMap.h>
#include <LibURL/Origin.h>

namespace Web::HTML {

class WindowProxy;

enum class WindowProxyAccessMode {
    SameOriginDomain,
    CrossOrigin,
};

struct LocalWindowProxyTarget {
    GC::Ptr<Window> window;

    Window& window_ref() const;
    WindowProxyAccessMode access_mode() const;
    Optional<URL::Origin> extract_an_origin() const;
    GC::Ref<BrowsingContext> associated_browsing_context() const;
    JS::ThrowCompletionOr<JS::Object*> internal_get_prototype_of() const;
    JS::ThrowCompletionOr<Optional<JS::PropertyDescriptor>> internal_get_own_property(WindowProxy const&, JS::PropertyKey const&) const;
    JS::ThrowCompletionOr<bool> internal_define_own_property(WindowProxy&, JS::PropertyKey const&, JS::PropertyDescriptor&) const;
    JS::ThrowCompletionOr<JS::Value> internal_get(WindowProxy const&, JS::PropertyKey const&, JS::Value receiver) const;
    JS::ThrowCompletionOr<bool> internal_set(WindowProxy&, JS::PropertyKey const&, JS::Value value, JS::Value receiver) const;
    JS::ThrowCompletionOr<bool> internal_delete(WindowProxy&, JS::PropertyKey const&) const;
    JS::ThrowCompletionOr<GC::RootVector<JS::Value>> internal_own_property_keys(WindowProxy const&) const;
};

struct RemoteWindowProxyTarget {
    GC::Ptr<Navigable> navigable;
    GC::Ptr<JS::Object> location;

    Navigable& navigable_ref() const;
    WindowProxyAccessMode access_mode() const;
    Optional<URL::Origin> extract_an_origin() const;
    GC::Ref<JS::Object> location_object(WindowProxy&);
    GC::Ref<BrowsingContext> associated_browsing_context() const;
    JS::ThrowCompletionOr<JS::Object*> internal_get_prototype_of() const;
    JS::ThrowCompletionOr<Optional<JS::PropertyDescriptor>> internal_get_own_property(WindowProxy const&, JS::PropertyKey const&) const;
    JS::ThrowCompletionOr<bool> internal_define_own_property(WindowProxy&, JS::PropertyKey const&, JS::PropertyDescriptor&) const;
    JS::ThrowCompletionOr<JS::Value> internal_get(WindowProxy const&, JS::PropertyKey const&, JS::Value receiver) const;
    JS::ThrowCompletionOr<bool> internal_set(WindowProxy&, JS::PropertyKey const&, JS::Value value, JS::Value receiver) const;
    JS::ThrowCompletionOr<bool> internal_delete(WindowProxy&, JS::PropertyKey const&) const;
    JS::ThrowCompletionOr<GC::RootVector<JS::Value>> internal_own_property_keys(WindowProxy const&) const;
};

class WEB_API WindowProxy final : public DOM::EventTarget {
    WEB_NON_IDL_PLATFORM_OBJECT(WindowProxy, DOM::EventTarget)
    GC_DECLARE_ALLOCATOR(WindowProxy);

public:
    virtual ~WindowProxy() override = default;

    virtual JS::ThrowCompletionOr<JS::Object*> internal_get_prototype_of() const override;
    virtual JS::ThrowCompletionOr<bool> internal_set_prototype_of(Object* prototype) override;
    virtual JS::ThrowCompletionOr<bool> internal_is_extensible() const override;
    virtual JS::ThrowCompletionOr<bool> internal_prevent_extensions() override;
    virtual JS::ThrowCompletionOr<Optional<JS::PropertyDescriptor>> internal_get_own_property(JS::PropertyKey const&) const override;
    virtual JS::ThrowCompletionOr<bool> internal_define_own_property(JS::PropertyKey const&, JS::PropertyDescriptor&, Optional<JS::PropertyDescriptor>* precomputed_get_own_property = nullptr) override;
    virtual JS::ThrowCompletionOr<JS::Value> internal_get(JS::PropertyKey const&, JS::Value receiver, JS::CacheableGetPropertyMetadata*, PropertyLookupPhase) const override;
    virtual JS::ThrowCompletionOr<bool> internal_set(JS::PropertyKey const&, JS::Value value, JS::Value receiver, JS::CacheableSetPropertyMetadata*, PropertyLookupPhase) override;
    virtual JS::ThrowCompletionOr<bool> internal_delete(JS::PropertyKey const&) override;
    virtual JS::ThrowCompletionOr<GC::RootVector<JS::Value>> internal_own_property_keys() const override;

    GC::Ptr<Window> window() const;
    void set_window(GC::Ref<Window>);
    GC::Ptr<Navigable> remote_navigable() const;
    GC::Ref<JS::Object> remote_location_object();
    CrossOriginPropertyDescriptorMap const& cross_origin_property_descriptor_map() const { return m_cross_origin_property_descriptor_map; }
    CrossOriginPropertyDescriptorMap& cross_origin_property_descriptor_map() { return m_cross_origin_property_descriptor_map; }

    GC::Ref<BrowsingContext> associated_browsing_context() const;

    virtual Optional<URL::Origin> extract_an_origin() const override;

private:
    explicit WindowProxy(JS::Realm&);
    static GC::Ref<WindowProxy> create_remote(JS::Realm&, GC::Ref<Navigable>);
    void set_remote_navigable(GC::Ref<Navigable>);

    LocalWindowProxyTarget* local_target();
    LocalWindowProxyTarget const* local_target() const;
    RemoteWindowProxyTarget* remote_target();
    RemoteWindowProxyTarget const* remote_target() const;

    virtual bool is_universal_global_scope_mixin() const final { return true; }

    virtual bool is_html_window_proxy() const override { return true; }
    virtual void visit_edges(JS::Cell::Visitor&) override;

    Variant<Empty, LocalWindowProxyTarget, RemoteWindowProxyTarget> m_target;
    CrossOriginPropertyDescriptorMap m_cross_origin_property_descriptor_map;

    friend class Navigable;
};

}

template<>
inline bool JS::Object::fast_is<Web::HTML::WindowProxy>() const { return is_html_window_proxy(); }
