/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/String.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

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

    String to_string() const;
};

enum class PatternProcessType {
    Pattern,
    URL,
};

WebIDL::ExceptionOr<URLPatternInit> process_a_url_pattern_init(URLPatternInit const&, PatternProcessType type,
    Optional<String> const& protocol, Optional<String> const& username, Optional<String> const& password,
    Optional<String> const& hostname, Optional<String> const& port, Optional<String> const& pathname,
    Optional<String> const& search, Optional<String> const& hash);

}
