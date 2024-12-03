/*
 * Copyright (c) 2023, Linus Groh <linusg@serenityos.org>
 * Copyright (c) 2023, Luke Wilde <lukew@serenityos.org>
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Forward.h>
#include <AK/String.h>
#include <LibJS/Runtime/Value.h>
#include <LibWeb/Forward.h>
#include <LibWeb/HTML/Scripting/ImportMap.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/webappapis.html#specifier-resolution-record
// A specifier resolution record is a struct. It has the following items:
struct SpecifierResolution {
    // A serialized base URL
    //    A string-or-null that represents the base URL of the specifier, when one exists.
    Optional<String> serialized_base_url;

    // A specifier
    //     A string representing the specifier.
    String specifier;

    // A specifier as a URL
    //     A URL-or-null that represents the URL in case of a URL-like module specifier.
    Optional<URL::URL> specifier_as_a_url;
};

// https://whatpr.org/html/9893/webappapis.html#universalglobalscope-mixin
class UniversalGlobalScopeMixin {
public:
    virtual ~UniversalGlobalScopeMixin();

    virtual DOM::EventTarget& this_impl() = 0;
    virtual DOM::EventTarget const& this_impl() const = 0;

    WebIDL::ExceptionOr<String> btoa(String const& data) const;
    WebIDL::ExceptionOr<String> atob(String const& data) const;
    void queue_microtask(WebIDL::CallbackType&);
    WebIDL::ExceptionOr<JS::Value> structured_clone(JS::Value, StructuredSerializeOptions const&) const;

    GC::Ref<WebIDL::CallbackType> count_queuing_strategy_size_function();
    GC::Ref<WebIDL::CallbackType> byte_length_queuing_strategy_size_function();

    void push_onto_outstanding_rejected_promises_weak_set(JS::Promise*);

    // Returns true if removed, false otherwise.
    bool remove_from_outstanding_rejected_promises_weak_set(JS::Promise*);

    void push_onto_about_to_be_notified_rejected_promises_list(GC::Ref<JS::Promise>);

    // Returns true if removed, false otherwise.
    bool remove_from_about_to_be_notified_rejected_promises_list(GC::Ref<JS::Promise>);

    void notify_about_rejected_promises(Badge<EventLoop>);

    ImportMap const& import_map() const { return m_import_map; }
    void set_import_map(ImportMap const& import_map) { m_import_map = import_map; }

    void append_resolved_module(SpecifierResolution resolution) { m_resolved_module_set.append(move(resolution)); }

protected:
    void visit_edges(GC::Cell::Visitor&);

private:
    // https://streams.spec.whatwg.org/#count-queuing-strategy-size-function
    GC::Ptr<WebIDL::CallbackType> m_count_queuing_strategy_size_function;

    // https://streams.spec.whatwg.org/#byte-length-queuing-strategy-size-function
    GC::Ptr<WebIDL::CallbackType> m_byte_length_queuing_strategy_size_function;

    // https://html.spec.whatwg.org/multipage/webappapis.html#about-to-be-notified-rejected-promises-list
    Vector<GC::Root<JS::Promise>> m_about_to_be_notified_rejected_promises_list;

    // https://html.spec.whatwg.org/multipage/webappapis.html#outstanding-rejected-promises-weak-set
    // The outstanding rejected promises weak set must not create strong references to any of its members, and implementations are free to limit its size, e.g. by removing old entries from it when new ones are added.
    Vector<GC::Ptr<JS::Promise>> m_outstanding_rejected_promises_weak_set;

    // https://html.spec.whatwg.org/multipage/webappapis.html#concept-global-import-map
    // A global object has an import map, initially an empty import map.
    ImportMap m_import_map;

    // https://html.spec.whatwg.org/multipage/webappapis.html#resolved-module-set
    // A global object has a resolved module set, a set of specifier resolution records, initially empty.
    //
    // NOTE: The resolved module set ensures that module specifier resolution returns the same result when called
    //       multiple times with the same (referrer, specifier) pair. It does that by ensuring that import map rules
    //       that impact the specifier in its referrer's scope cannot be defined after its initial resolution. For now,
    //       only Window global objects have their module set data structures modified from the initial empty one.
    Vector<SpecifierResolution> m_resolved_module_set;
};

}
