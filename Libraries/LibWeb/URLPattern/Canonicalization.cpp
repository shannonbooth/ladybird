/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibURL/Parser.h>
#include <LibWeb/URLPattern/Canonicalization.h>

namespace Web::URLPattern {

// https://urlpattern.spec.whatwg.org/#canonicalize-a-protocol
WebIDL::ExceptionOr<String> canonicalize_a_protocol(String const& value)
{
    // 1. If value is the empty string, return value.
    if (value.is_empty())
        return value;

    // 2. Let dummyURL be a new URL record.
    URL::URL dummy_url;

    // 3. Let parseResult be the result of running the basic URL parser given value followed by "://dummy.test", with dummyURL as url.
    //
    // Spec-Note: Note, state override is not used here because it enforces restrictions that are only appropriate for the
    //            protocol setter. Instead we use the protocol to parse a dummy URL using the normal parsing entry point.
    auto parse_result = URL::Parser::basic_parse(MUST(String::formatted("{}://dummy.test"sv, value)), {}, &dummy_url);

    // 4. If parseResult is failure, then throw a TypeError.
    if (!parse_result.has_value())
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Failed to canonicalize URL protocol string"sv };

    // 5. Return dummyURL’s scheme.
    return dummy_url.scheme();
}

// https://urlpattern.spec.whatwg.org/#canonicalize-a-username
String canonicalize_a_username(String const& value)
{
    // 1. If value is the empty string, return value.
    if (value.is_empty())
        return value;

    // 2. Let dummyURL be a new URL record.
    URL::URL dummy_url;

    // 3. Set the username given dummyURL and value.
    dummy_url.set_username(value);

    // 4. Return dummyURL’s username.
    return dummy_url.username();
}

// https://urlpattern.spec.whatwg.org/#canonicalize-a-password
String canonicalize_a_password(String const& value)
{
    // 1. If value is the empty string, return value.
    if (value.is_empty())
        return value;

    // 2. Let dummyURL be a new URL record.
    URL::URL dummy_url;

    // 3. Set the password given dummyURL and value.
    dummy_url.set_password(value);

    // 4. Return dummyURL’s password.
    return dummy_url.password();
}

// https://urlpattern.spec.whatwg.org/#canonicalize-a-hostname
WebIDL::ExceptionOr<String> canonicalize_a_hostname(String const& value)
{
    // 1. If value is the empty string, return value.
    if (value.is_empty())
        return value;

    // 2. Let dummyURL be a new URL record.
    URL::URL dummy_url;

    // 3. Let parseResult be the result of running the basic URL parser given value with dummyURL as url and hostname state as state override.
    auto parse_result = URL::Parser::basic_parse(value, {}, &dummy_url, URL::Parser::State::Hostname);

    // 4. If parseResult is failure, then throw a TypeError.
    if (!parse_result.has_value())
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Failed to canonicalize URL hostname string"sv };

    // 5. Return dummyURL’s host, serialized, or empty string if it is null.
    if (!dummy_url.host().has_value())
        return String {};
    return dummy_url.host()->serialize();
}

// https://urlpattern.spec.whatwg.org/#canonicalize-an-ipv6-hostname
WebIDL::ExceptionOr<String> canonicalize_an_ipv6_hostname(String const& value)
{
    // 1. Let result be the empty string.
    StringBuilder result;

    // 2. For each code point in value interpreted as a list of code points:
    for (auto code_point : value.code_points()) {
        // 1. If all of the following are true:
        //     * code point is not an ASCII hex digit;
        //     * code point is not U+005B ([);
        //     * code point is not U+005D (]); and
        //     * code point is not U+003A (:),
        //    then throw a TypeError.
        if (!is_ascii_hex_digit(code_point)
            && code_point != '['
            && code_point != ']'
            && code_point != ':') {
            return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Failed to canonicalize ipv6 hostname string"sv };
        }

        // 2. Append the result of running ASCII lowercase given code point to the end of result.
        result.append(to_ascii_lowercase(code_point));
    }

    // 3. Return result.
    return result.to_string_without_validation();
}

// https://urlpattern.spec.whatwg.org/#canonicalize-a-port
WebIDL::ExceptionOr<String> canonicalize_a_port(String const& port_value, Optional<String> const& protocol_value)
{
    // 1. If portValue is the empty string, return portValue.
    if (port_value.is_empty())
        return port_value;

    // 2. Let dummyURL be a new URL record.
    URL::URL dummy_url;

    // 3. If protocolValue was given, then set dummyURL’s scheme to protocolValue.
    // Spec-Note: Note, we set the URL record's scheme in order for the basic URL parser to recognize and normalize default port values.
    if (protocol_value.has_value())
        dummy_url.set_scheme(protocol_value.value());

    // 4. Let parseResult be the result of running basic URL parser given portValue with dummyURL as url and port state as state override.
    auto parse_result = URL::Parser::basic_parse(port_value, {}, &dummy_url, URL::Parser::State::Port);

    // 4. If parseResult is failure, then throw a TypeError.
    if (!parse_result.has_value())
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Failed to canonicalize port string"sv };

    // 5. Return dummyURL’s port, serialized, or empty string if it is null.
    if (!dummy_url.port().has_value())
        return String {};
    return String::number(*dummy_url.port());
}

// https://urlpattern.spec.whatwg.org/#canonicalize-a-pathname
WebIDL::ExceptionOr<String> canonicalize_a_pathname(String const& value)
{
    // 1. If value is the empty string, then return value.
    if (value.is_empty())
        return value;

    // 2. Let leading slash be true if the first code point in value is U+002F (/) and otherwise false.
    bool leading_slash = value.bytes()[0] == '/';

    // 3. Let modified value be "/-" if leading slash is false and otherwise the empty string.
    StringBuilder modified_value;
    if (!leading_slash)
        modified_value.append("/-"sv);

    // 4. Append value to the end of modified value.
    modified_value.append(value);

    // 5. Let dummyURL be a new URL record.
    URL::URL dummy_url;

    // 6. Let parseResult be the result of running basic URL parser given modified value with dummyURL as url and path start state as state override.
    auto parse_result = URL::Parser::basic_parse(value, {}, &dummy_url, URL::Parser::State::PathStart);

    // 7. If parseResult is failure, then throw a TypeError.
    if (!parse_result.has_value())
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Failed to canonicalize pathname string"sv };

    // 8. Let result be the result of URL path serializing dummyURL.
    auto result = dummy_url.serialize_path();

    // 9. If leading slash is false, then set result to the code point substring from 2 to the end of the string within result.
    if (!leading_slash)
        result = MUST(String::from_utf8(result.code_points().unicode_substring_view(2).as_string()));

    // 10. Return result.
    return result;
}

// https://urlpattern.spec.whatwg.org/#canonicalize-an-opaque-pathname
WebIDL::ExceptionOr<String> canonicalize_an_opaque_pathname(String const& value)
{
    // 1. If value is the empty string, return value.
    if (value.is_empty())
        return value;

    // 2. Let dummyURL be a new URL record.
    URL::URL dummy_url;

    // 3. Set dummyURL’s path to the empty string.
    //
    // FIXME: Spec bug: https://github.com/whatwg/urlpattern/issues/242
    //        I am not 100% sure this is correct, but I assume it means set URL path to a list containing an empty string?
    dummy_url.set_paths({ "" });

    // 4. Let parseResult be the result of running URL parsing given value with dummyURL as url and opaque path state as state override.
    // FIXME: Editorial spec issue, it should use consistent language, and state to run the basic URL parser.
    // FIXME: Update the URL parser state to the 'opaque' state.
    auto parse_result = URL::Parser::basic_parse(value, {}, &dummy_url, URL::Parser::State::CannotBeABaseUrlPath);

    // 5. If parseResult is failure, then throw a TypeError.
    if (!parse_result.has_value())
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Failed to canonicalize opaque pathname string"sv };

    // 6. Return the result of URL path serializing dummyURL.
    return dummy_url.serialize_path();
}

// https://urlpattern.spec.whatwg.org/#canonicalize-a-search
WebIDL::ExceptionOr<String> canonicalize_a_search(String const& value)
{
    // 1. If value is the empty string, return value.
    if (value.is_empty())
        return value;

    // 2. Let dummyURL be a new URL record.
    URL::URL dummy_url;

    // 3. Set dummyURL’s query to the empty string.
    dummy_url.set_query(String {});

    // 4. Let parseResult be the result of running basic URL parser given value with dummyURL as url and query state as state override.
    auto parse_result = URL::Parser::basic_parse(value, {}, &dummy_url, URL::Parser::State::Query);

    // 5. If parseResult is failure, then throw a TypeError.
    if (!parse_result.has_value())
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Failed to canonicalize query string"sv };

    // 6. Return dummyURL’s query.
    // FIXME: We should likely change spec to verify that query is not null here.
    return *dummy_url.query();
}

// https://urlpattern.spec.whatwg.org/#canonicalize-a-hash
WebIDL::ExceptionOr<String> canonicalize_a_hash(String const& value)
{
    // 1. If value is the empty string, return value.
    if (value.is_empty())
        return value;

    // 2. Let dummyURL be a new URL record.
    URL::URL dummy_url;

    // 3. Set dummyURL’s fragment to the empty string.
    dummy_url.set_fragment(String {});

    // 4. Let parseResult be the result of running basic URL parser given value with dummyURL as url and fragment state as state override.
    auto parse_result = URL::Parser::basic_parse(value, {}, &dummy_url, URL::Parser::State::Fragment);

    // 5. If parseResult is failure, then throw a TypeError.
    if (!parse_result.has_value())
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Failed to canonicalize query string"sv };

    // 6. Return dummyURL’s fragment.
    VERIFY(dummy_url.fragment().has_value());
    // FIXME: We should likely change spec to verify that fragment is not null here.
    return dummy_url.fragment().value();
}

}
