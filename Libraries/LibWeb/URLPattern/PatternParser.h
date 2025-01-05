/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// FIXME: Maybe Token.h should be split from Tokenizer.h
#include <AK/Function.h>
#include <LibWeb/URLPattern/Tokenizer.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

namespace Web::URLPattern {

// FIXME: Maybe move to new header?
// https://urlpattern.spec.whatwg.org/#part
struct Part {
    // https://urlpattern.spec.whatwg.org/#part-type
    enum class Type {
        // The part represents a simple fixed text string.
        FixedText,

        // The part represents a matching group with a custom regular expression.
        Regexp,

        // The part represents a matching group that matches code points up to the next separator code point. This is
        // typically used for a named group like ":foo" that does not have a custom regular expression.
        SegmentWildcard,

        // The part represents a matching group that greedily matches all code points. This is typically used for
        // the "*" wildcard matching group.
        FullWildcard,
    };

    // https://urlpattern.spec.whatwg.org/#part-modifier
    enum class Modifier {
        // The part does not have a modifier.
        None,

        // The part has an optional modifier indicated by the U+003F (?) code point.
        Optional,

        // The part has a "zero or more" modifier indicated by the U+002A (*) code point.
        ZeroOrMore,

        // The part has a "one or more" modifier indicated by the U+002B (+) code point.
        OneOrMore,
    };

    static String convert_modifier_to_string(Modifier);
    static String convert_type_to_string(Type);

    Part(Type, String value, Modifier);
    Part(Type, String value, Modifier, String name, String prefix, String suffix);

    // https://urlpattern.spec.whatwg.org/#part-type
    // A part has an associated type, a string, which must be set upon creation.
    Type type {};

    // https://urlpattern.spec.whatwg.org/#part-value
    // A part has an associated value, a string, which must be set upon creation.
    String value;

    // https://urlpattern.spec.whatwg.org/#part-modifier
    // A part has an associated modifier a string, which must be set upon creation.
    Modifier modifier;

    // https://urlpattern.spec.whatwg.org/#part-name
    // A part has an associated name, a string, initially the empty string.
    String name;

    // https://urlpattern.spec.whatwg.org/#part-prefix
    // A part has an associated prefix, a string, initially the empty string.
    String prefix;

    // https://urlpattern.spec.whatwg.org/#part-suffix
    // A part has an associated suffix, a string, initially the empty string.
    String suffix;
};

// https://urlpattern.spec.whatwg.org/#pattern-parser
class PatternParser {
public:
    // https://urlpattern.spec.whatwg.org/#options
    struct Options {
        // https://urlpattern.spec.whatwg.org/#options-delimiter-code-point
        Optional<char> delimiter_code_point;

        // https://urlpattern.spec.whatwg.org/#options-prefix-code-point
        Optional<char> prefix_code_point;

        // https://urlpattern.spec.whatwg.org/#options-ignore-case
        bool ignore_case { false };
    };

    static PatternParser::Options const default_options;
    static PatternParser::Options const hostname_options;
    static PatternParser::Options const pathname_options;

    // https://urlpattern.spec.whatwg.org/#encoding-callback
    // An encoding callback is an abstract algorithm that takes a given string input. The input will be a simple text
    // piece of a pattern string. An implementing algorithm will validate and encode the input. It must return the
    // encoded string or throw an exception.
    using EncodingCallback = Function<WebIDL::ExceptionOr<String>(String const&)>;

    static WebIDL::ExceptionOr<Vector<Part>> parse(Utf8View const& input, Options const&, EncodingCallback);

private:
    PatternParser(EncodingCallback, String segment_wildcard_regexp);

    Token const* try_to_consume_a_token(Token::Type);
    Token const* try_to_consume_a_modifier_token();
    Token const* try_to_consume_a_regexp_or_wildcard_token(Token const* name_token);
    WebIDL::ExceptionOr<void> consume_a_required_token(Token::Type);
    String consume_text();
    WebIDL::ExceptionOr<void> maybe_add_a_part_from_the_pending_fixed_value();
    WebIDL::ExceptionOr<void> add_a_part(String const& prefix, Token const* name_token,
        Token const* regexp_or_wildcard_token, String const& suffix, Token const* modifier_token);
    bool is_a_duplicate_name(String const&) const;

    // https://urlpattern.spec.whatwg.org/#pattern-parser-token-list
    // A pattern parser has an associated token list, a token list, initially an empty list.
    Vector<Token> m_token_list;

    // https://urlpattern.spec.whatwg.org/#pattern-parser-encoding-callback
    // A pattern parser has an associated encoding callback, a encoding callback, that must be set upon creation.
    EncodingCallback m_encoding_callback;

    // https://urlpattern.spec.whatwg.org/#pattern-parser-segment-wildcard-regexp
    // A pattern parser has an associated segment wildcard regexp, a string, that must be set upon creation.
    String m_segment_wildcard_regexp;

    // https://urlpattern.spec.whatwg.org/#pattern-parser-part-list
    // A pattern parser has an associated part list, a part list, initially an empty list.
    Vector<Part> m_part_list;

    // https://urlpattern.spec.whatwg.org/#pattern-parser-pending-fixed-value
    // A pattern parser has an associated pending fixed value, a string, initially the empty string.
    StringBuilder m_pending_fixed_value;

    // https://urlpattern.spec.whatwg.org/#pattern-parser-index
    // A pattern parser has an associated index, a number, initially 0.
    size_t m_index { 0 };

    // https://urlpattern.spec.whatwg.org/#pattern-parser-next-numeric-name
    // A pattern parser has an associated next numeric name, a number, initially 0.
    size_t m_next_numeric_name { 0 };
};

}
