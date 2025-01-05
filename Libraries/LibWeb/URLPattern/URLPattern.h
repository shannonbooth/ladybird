/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/String.h>
#include <LibWeb/Bindings/PlatformObject.h>
#include <LibWeb/URLPattern/Component.h>
#include <LibWeb/URLPattern/URLPatternInit.h>

namespace Web::URLPattern {

// https://urlpattern.spec.whatwg.org/#dictdef-urlpatternoptions
struct URLPatternOptions {
    bool ignore_case { false };
};

using URLPatternInput = Variant<String, URLPatternInit>;

// https://urlpattern.spec.whatwg.org/#dictdef-urlpatterncomponentresult
struct URLPatternComponentResult {
    String input;

    OrderedHashMap<String, Variant<String, Empty>> groups;
};

// https://urlpattern.spec.whatwg.org/#dictdef-urlpatternresult
struct URLPatternResult {
    Vector<URLPatternInput> inputs;

    URLPatternComponentResult protocol;
    URLPatternComponentResult username;
    URLPatternComponentResult password;
    URLPatternComponentResult hostname;
    URLPatternComponentResult port;
    URLPatternComponentResult pathname;
    URLPatternComponentResult search;
    URLPatternComponentResult hash;
};

// https://urlpattern.spec.whatwg.org/#url-pattern
struct URLPatternRecord {
public:
    static WebIDL::ExceptionOr<URLPatternRecord> create(URLPatternInput const&, Optional<String> const& base_url = {}, URLPatternOptions const& = {});

    bool has_regexp_groups() const;
    WebIDL::ExceptionOr<Optional<URLPatternResult>> match(URLPatternInput const&, Optional<String> const&) const;

    Component const& protocol_component() const { return m_protocol_component; }
    Component const& username_component() const { return m_username_component; }
    Component const& password_component() const { return m_password_component; }
    Component const& hostname_component() const { return m_hostname_component; }
    Component const& port_component() const { return m_port_component; }
    Component const& pathname_component() const { return m_pathname_component; }
    Component const& search_component() const { return m_search_component; }
    Component const& hash_component() const { return m_hash_component; }

private:
    // https://urlpattern.spec.whatwg.org/#url-pattern-protocol-component
    // protocol component, a component
    Component m_protocol_component;

    // https://urlpattern.spec.whatwg.org/#url-pattern-username-component
    // username component, a component
    Component m_username_component;

    // https://urlpattern.spec.whatwg.org/#url-pattern-password-component
    // password component, a component
    Component m_password_component;

    // https://urlpattern.spec.whatwg.org/#url-pattern-hostname-component
    // hostname component, a component
    Component m_hostname_component;

    // https://urlpattern.spec.whatwg.org/#url-pattern-port-component
    // port component, a component
    Component m_port_component;

    // https://urlpattern.spec.whatwg.org/#url-pattern-pathname-component
    // pathname component, a component
    Component m_pathname_component;

    // https://urlpattern.spec.whatwg.org/#url-pattern-search-component
    // search component, a component
    Component m_search_component;

    // https://urlpattern.spec.whatwg.org/#url-pattern-hash-component
    // hash component, a component
    Component m_hash_component;
};

// https://urlpattern.spec.whatwg.org/#urlpattern
class URLPattern : public Bindings::PlatformObject {
    WEB_PLATFORM_OBJECT(URLPattern, Bindings::PlatformObject);
    GC_DECLARE_ALLOCATOR(URLPattern);

public:
    static WebIDL::ExceptionOr<GC::Ref<URLPattern>> create(JS::Realm&, URLPatternInput const&, Optional<String> const& base_url, URLPatternOptions const& = {});
    static WebIDL::ExceptionOr<GC::Ref<URLPattern>> construct_impl(JS::Realm&, URLPatternInput const&, String const& base_url, URLPatternOptions const& = {});
    static WebIDL::ExceptionOr<GC::Ref<URLPattern>> construct_impl(JS::Realm&, URLPatternInput const&, URLPatternOptions const& = {});

    String const& protocol() const;
    String const& username() const;
    String const& password() const;
    String const& hostname() const;
    String const& port() const;
    String const& pathname() const;
    String const& search() const;
    String const& hash() const;

    bool has_reg_exp_groups() const;

    WebIDL::ExceptionOr<bool> test(URLPatternInput const&, Optional<String> const& base_url) const;
    WebIDL::ExceptionOr<Optional<URLPatternResult>> exec(URLPatternInput const&, Optional<String> const& base_url) const;

    virtual ~URLPattern() override;

protected:
    virtual void initialize(JS::Realm&) override;

    explicit URLPattern(JS::Realm&, URLPatternRecord);

private:
    // https://urlpattern.spec.whatwg.org/#ref-for-url-pattern%E2%91%A0
    // Each URLPattern has an associated URL pattern, a URL pattern.
    URLPatternRecord m_url_pattern;
};

}
