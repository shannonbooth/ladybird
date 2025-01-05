/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/String.h>
#include <LibWeb/Bindings/PlatformObject.h>

namespace Web::URLPattern {

// https://urlpattern.spec.whatwg.org/#dictdef-urlpatterninit
struct URLPatternInit {
    Optional<String> protocol;
    Optional<String> username;
    Optional<String> password;
    Optional<String> hostname;
    Optional<String> port;
    Optional<String> pathname;
    Optional<String> search;
    Optional<String> hash;
    Optional<String> base_url;
};

// https://urlpattern.spec.whatwg.org/#dictdef-urlpatternoptions
struct URLPatternOptions {
    bool ignore_case { false };
};

using URLPatternInput = Variant<String, URLPatternInit>;

// https://urlpattern.spec.whatwg.org/#urlpattern
class URLPattern : public Bindings::PlatformObject {
    WEB_PLATFORM_OBJECT(URLPattern, Bindings::PlatformObject);
    GC_DECLARE_ALLOCATOR(URLPattern);

public:
    static WebIDL::ExceptionOr<GC::Ref<URLPattern>> construct_impl(JS::Realm&, URLPatternInput const&, String const& base_url, URLPatternOptions const&);

    virtual ~URLPattern() override;

protected:
    virtual void initialize(JS::Realm&) override;

    explicit URLPattern(JS::Realm&);
};

}
