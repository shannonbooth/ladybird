/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <AK/Vector.h>
#include <LibRegex/Regex.h>
#include <LibWeb/URLPattern/PatternParser.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

namespace Web::URLPattern {

// https://urlpattern.spec.whatwg.org/#component
struct Component {
    // https://urlpattern.spec.whatwg.org/#component-pattern-string
    // pattern string, a well formed pattern string
    String pattern_string;

    // https://urlpattern.spec.whatwg.org/#component-regular-expression
    // regular expression, a RegExp
    //
    // NOTE: This is optional to allow default construct.
    Optional<Regex<ECMA262>> regular_expression;

    // https://urlpattern.spec.whatwg.org/#component-group-name-list
    // group name list, a list of strings
    Vector<String> group_name_list;

    // https://urlpattern.spec.whatwg.org/#component-has-regexp-groups
    // has regexp groups, a boolean
    bool has_regexp_groups {};

    static WebIDL::ExceptionOr<Component> compile(Utf8View const& input, PatternParser::EncodingCallback, PatternParser::Options const&);
};

String generate_a_segment_wildcard_regexp(PatternParser::Options const&);

}
