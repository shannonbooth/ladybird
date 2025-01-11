/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibURL/Parser.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/URLPatternPrototype.h>
#include <LibWeb/URLPattern/Canonicalization.h>
#include <LibWeb/URLPattern/ConstructorStringParser.h>
#include <LibWeb/URLPattern/URLPattern.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

namespace Web::URLPattern {

GC_DEFINE_ALLOCATOR(URLPattern);

URLPattern::URLPattern(JS::Realm& realm, URLPatternRecord url_pattern)
    : PlatformObject(realm)
    , m_url_pattern(move(url_pattern))
{
}

// https://urlpattern.spec.whatwg.org/#dom-urlpattern-urlpattern
WebIDL::ExceptionOr<GC::Ref<URLPattern>> URLPattern::construct_impl(JS::Realm& realm, URLPatternInput const& input, String const& base_url, URLPatternOptions const& options)
{
    // 1. Run initialize given this, input, baseURL, and options.
    return create(realm, input, base_url, options);
}

// https://urlpattern.spec.whatwg.org/#dom-urlpattern-urlpattern-input-options
WebIDL::ExceptionOr<GC::Ref<URLPattern>> URLPattern::construct_impl(JS::Realm& realm, URLPatternInput const& input, URLPatternOptions const& options)
{
    // 1. Run initialize given this, input, null, and options.
    return create(realm, input, {}, options);
}

// https://urlpattern.spec.whatwg.org/#urlpattern-initialize
WebIDL::ExceptionOr<GC::Ref<URLPattern>> URLPattern::create(JS::Realm& realm, URLPatternInput const& input, Optional<String> const& base_url, URLPatternOptions const& options)
{
    // Set this’s associated URL pattern to the result of create given input, baseURL, and options.
    return realm.create<URLPattern>(realm, TRY(URLPatternRecord::create(input, base_url, options)));
}

// https://urlpattern.spec.whatwg.org/#protocol-component-matches-a-special-scheme
static bool protocol_component_matches_a_special_scheme(Component const& protocol_component)
{
    // 1. Let special scheme list be a list populated with all of the special schemes.
    // 2. For each scheme of special scheme list:
    for (StringView scheme : URL::special_schemes()) {
        // 1. Let test result be RegExpBuiltinExec(protocol component’s regular expression, scheme).
        auto test_result = protocol_component.regular_expression->match(scheme);

        // 2. If test result is not null, then return true.
        if (test_result.success)
            return true;
    }

    // 3. Return false.
    return false;
}

// https://urlpattern.spec.whatwg.org/#hostname-pattern-is-an-ipv6-address
static bool hostname_pattern_is_an_ipv6_address(String const& input)
{
    // 1. If input’s code point length is less than 2, then return false.
    if (input.bytes().size() < 2)
        return false;

    // 2. Let input code points be input interpreted as a list of code points.
    auto input_code_points = input.bytes();

    // 3. If input code points[0] is U+005B ([), then return true.
    if (input_code_points[0] == '[')
        return true;

    // 4. If input code points[0] is U+007B ({) and input code points[1] is U+005B ([), then return true.
    if (input_code_points[0] == '{' && input_code_points[1] == '[')
        return true;

    // 5. If input code points[0] is U+005C (\) and input code points[1] is U+005B ([), then return true.
    if (input_code_points[0] == '\\' && input_code_points[1] == '[')
        return true;

    // 6. Return false.
    return false;
}

// https://urlpattern.spec.whatwg.org/#url-pattern-create
WebIDL::ExceptionOr<URLPatternRecord> URLPatternRecord::create(URLPatternInput const& input, Optional<String> const& base_url, URLPatternOptions const& options)
{
    // 1. Let init be null.
    URLPatternInit init;

    // 2. If input is a scalar value string then:
    if (input.has<String>()) {
        // 1. Set init to the result of running parse a constructor string given input.
        init = TRY(ConstructorStringParser::parse(input.get<String>().code_points()));

        // 2. If baseURL is null and init["protocol"] does not exist, then throw a TypeError.
        if (!base_url.has_value() && !init.protocol.has_value())
            return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Relative URLPattern constructor must have one of baseURL or protocol"sv };

        // 3. If baseURL is not null, set init["baseURL"] to baseURL.
        if (base_url.has_value())
            init.base_url = base_url;
    }
    // 3. Otherwise:
    else {
        // 1. Assert: input is a URLPatternInit.
        VERIFY(input.has<URLPatternInit>());

        // 2. If baseURL is not null, then throw a TypeError.
        if (base_url.has_value())
            return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "baseURL should be provided through URLPatternInit.baseURL"sv };

        // 3. Set init to input.
        init = input.get<URLPatternInit>();
    }

    // 4. Let processedInit be the result of process a URLPatternInit given init, "pattern", null, null, null, null, null, null, null, and null.
    auto processed_init = TRY(process_a_url_pattern_init(init, PatternProcessType::Pattern, {}, {}, {}, {}, {}, {}, {}, {}));

    // 5. For each componentName of « "protocol", "username", "password", "hostname", "port", "pathname", "search", "hash" »:
    //     1. If processedInit[componentName] does not exist, then set processedInit[componentName] to "*".
    if (!processed_init.protocol.has_value())
        processed_init.protocol = "*"_string;
    if (!processed_init.username.has_value())
        processed_init.username = "*"_string;
    if (!processed_init.password.has_value())
        processed_init.password = "*"_string;
    if (!processed_init.hostname.has_value())
        processed_init.hostname = "*"_string;
    if (!processed_init.port.has_value())
        processed_init.port = "*"_string;
    if (!processed_init.pathname.has_value())
        processed_init.pathname = "*"_string;
    if (!processed_init.search.has_value())
        processed_init.search = "*"_string;
    if (!processed_init.hash.has_value())
        processed_init.hash = "*"_string;

    // 6. If processedInit["protocol"] is a special scheme and processedInit["port"] is a string which represents its
    //    corresponding default port in radix-10 using ASCII digits then set processedInit["port"] to the empty string.
    if (URL::is_special_scheme(processed_init.protocol.value())) {
        auto maybe_port = processed_init.port->to_number<u16>();
        if (maybe_port.has_value() && *maybe_port == URL::default_port_for_scheme(*processed_init.protocol).value())
            processed_init.port = String {};
    }

    // 7. Let urlPattern be a new URL pattern.
    URLPatternRecord url_pattern;

    // 8. Set urlPattern’s protocol component to the result of compiling a component given processedInit["protocol"],
    //    canonicalize a protocol, and default options.
    url_pattern.m_protocol_component = TRY(Component::compile(processed_init.protocol->code_points(), canonicalize_a_protocol, PatternParser::default_options));

    // 9. Set urlPattern’s username component to the result of compiling a component given processedInit["username"],
    //    canonicalize a username, and default options.
    url_pattern.m_username_component = TRY(Component::compile(processed_init.username->code_points(), canonicalize_a_username, PatternParser::default_options));

    // 10. Set urlPattern’s password component to the result of compiling a component given processedInit["password"],
    //     canonicalize a password, and default options.
    url_pattern.m_password_component = TRY(Component::compile(processed_init.password->code_points(), canonicalize_a_password, PatternParser::default_options));

    // 11. If the result running hostname pattern is an IPv6 address given processedInit["hostname"] is true, then set
    //     urlPattern’s hostname component to the result of compiling a component given processedInit["hostname"],
    //     canonicalize an IPv6 hostname, and hostname options.
    if (hostname_pattern_is_an_ipv6_address(processed_init.hostname.value())) {
        url_pattern.m_hostname_component = TRY(Component::compile(processed_init.hostname->code_points(), canonicalize_an_ipv6_hostname, PatternParser::hostname_options));
    }
    // 12. Otherwise, set urlPattern’s hostname component to the result of compiling a component given
    //     processedInit["hostname"], canonicalize a hostname, and hostname options.
    else {
        url_pattern.m_hostname_component = TRY(Component::compile(processed_init.hostname->code_points(), canonicalize_a_hostname, PatternParser::hostname_options));
    }

    // 13. Set urlPattern’s port component to the result of compiling a component given processedInit["port"],
    //     canonicalize a port, and default options.
    url_pattern.m_port_component = TRY(Component::compile(processed_init.port->code_points(), [](String const& value) { return canonicalize_a_port(value); }, PatternParser::default_options));

    // 14. Let compileOptions be a copy of the default options with the ignore case property set to options["ignoreCase"].
    auto compile_options = PatternParser::default_options;
    compile_options.ignore_case = options.ignore_case;

    // 15. If the result of running protocol component matches a special scheme given urlPattern’s protocol component is true, then:
    if (protocol_component_matches_a_special_scheme(url_pattern.m_protocol_component)) {
        // 1. Let pathCompileOptions be copy of the pathname options with the ignore case property set to options["ignoreCase"].
        auto path_compile_options = PatternParser::pathname_options;
        path_compile_options.ignore_case = options.ignore_case;

        // 2. Set urlPattern’s pathname component to the result of compiling a component given processedInit["pathname"],
        //    canonicalize a pathname, and pathCompileOptions.
        url_pattern.m_pathname_component = TRY(Component::compile(processed_init.pathname->code_points(), canonicalize_a_pathname, path_compile_options));
    }
    // 16. Otherwise set urlPattern’s pathname component to the result of compiling a component given
    //     processedInit["pathname"], canonicalize an opaque pathname, and compileOptions.
    else {
        url_pattern.m_pathname_component = TRY(Component::compile(processed_init.pathname->code_points(), canonicalize_an_opaque_pathname, compile_options));
    }

    // 17. Set urlPattern’s search component to the result of compiling a component given processedInit["search"],
    //     canonicalize a search, and compileOptions.
    url_pattern.m_search_component = TRY(Component::compile(processed_init.search->code_points(), canonicalize_a_search, compile_options));

    // 18. Set urlPattern’s hash component to the result of compiling a component given processedInit["hash"],
    //     canonicalize a hash, and compileOptions.
    url_pattern.m_hash_component = TRY(Component::compile(processed_init.hash->code_points(), canonicalize_a_hash, compile_options));

    // 19. Return urlPattern.
    return url_pattern;
}

// https://urlpattern.spec.whatwg.org/#create-a-component-match-result
static URLPatternComponentResult create_a_component_match_result(Component const& component, String const& input, regex::RegexResult const& exec_result)
{
    // 1. Let result be a new URLPatternComponentResult.
    URLPatternComponentResult result;

    // 2. Set result["input"] to input.
    result.input = input;

    // 3. Let groups be a record<USVString, (USVString or undefined)>.
    OrderedHashMap<String, Variant<String, JS::Value>> groups;

    // 4. Let index be 1.
    // 5. While index is less than Get(execResult, "length"):
    for (size_t index = 1; index <= exec_result.n_capture_groups; ++index) {
        auto& capture = exec_result.capture_group_matches[0][index];

        // 1. Let name be component’s group name list[index − 1].
        auto name = component.group_name_list[index - 1];

        // 2. Let value be Get(execResult, ToString(index)).
        // 3. Set groups[name] to value.
        if (capture.view.is_null())
            groups.set(name, JS::js_undefined());
        else
            groups.set(name, MUST(capture.view.to_string()));

        // 4. Increment index by 1.
    }

    // 6. Set result["groups"] to groups.
    result.groups = groups;

    // 7. Return result.
    return result;
}

// https://urlpattern.spec.whatwg.org/#url-pattern-match
WebIDL::ExceptionOr<Optional<URLPatternResult>> URLPatternRecord::match(URLPatternInput const& input, Optional<String> const& base_url_string) const
{
    // 1. Let protocol be the empty string.
    String protocol;

    // 2. Let username be the empty string.
    String username;

    // 3. Let password be the empty string.
    String password;

    // 4. Let hostname be the empty string.
    String hostname;

    // 5. Let port be the empty string.
    String port;

    // 6. Let pathname be the empty string.
    String pathname;

    // 7. Let search be the empty string.
    String search;

    // 8. Let hash be the empty string.
    String hash;

    // 9. Let inputs be an empty list.
    Vector<URLPatternInput> inputs;

    // 10. Append input to inputs.
    inputs.append(input);

    // 11. If input is a URLPatternInit then:
    if (input.has<URLPatternInit>()) {
        // 1. If baseURLString was given, throw a TypeError.
        if (base_url_string.has_value())
            return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Base URL cannot be provided when URLPatternInput is provided"sv };

        // 2. Let applyResult be the result of process a URLPatternInit given input, "url", protocol, username, password,
        //    hostname, port, pathname, search, and hash. If this throws an exception, catch it, and return null.
        auto apply_result_or_error = process_a_url_pattern_init(input.get<URLPatternInit>(), PatternProcessType::URL,
            protocol, username, password, hostname, port, pathname, search, hash);
        if (apply_result_or_error.is_error())
            return OptionalNone {};
        auto apply_result = apply_result_or_error.release_value();

        // 3. Set protocol to applyResult["protocol"].
        protocol = apply_result.protocol.value();

        // 4. Set username to applyResult["username"].
        username = apply_result.username.value();

        // 5. Set password to applyResult["password"].
        password = apply_result.password.value();

        // 6. Set hostname to applyResult["hostname"].
        hostname = apply_result.hostname.value();

        // 7. Set port to applyResult["port"].
        port = apply_result.port.value();

        // 8. Set pathname to applyResult["pathname"].
        pathname = apply_result.pathname.value();

        // 9. Set search to applyResult["search"].
        search = apply_result.search.value();

        // 10. Set hash to applyResult["hash"].
        hash = apply_result.hash.value();
    }
    // 12. Otherwise:
    else {
        // 1. Let url be input.
        URL::URL url;

        // 2. If input is a USVString:
        if (input.has<String>()) {
            // 1. Let baseURL be null.
            Optional<URL::URL> base_url;

            // 2. If baseURLString was given, then:
            if (base_url_string.has_value()) {
                // 1. Set baseURL to the result of running the basic URL parser on baseURLString.
                base_url = URL::Parser::basic_parse(base_url_string.value());

                // 2. If baseURL is failure, return null.
                if (!base_url->is_valid())
                    return OptionalNone {};

                // 3. Append baseURLString to inputs.
                inputs.append(base_url_string.value());
            }

            // 3. Set url to the result of parsing input given baseURL.
            // Set url to the result of running the basic URL parser on input with baseURL.
            url = URL::Parser::basic_parse(input.get<String>(), base_url);

            // 4. If url is failure, return null.
            if (!url.is_valid())
                return OptionalNone {};
        }

        // 3. Assert: url is a URL.
        VERIFY(url.is_valid());

        // 4. Set protocol to url’s scheme.
        protocol = url.scheme();

        // 5. Set username to url’s username.
        username = url.username();

        // 6. Set password to url’s password.
        password = url.password();

        // 7. Set hostname to url’s host, serialized, or the empty string if the value is null.
        if (url.host().has_value())
            hostname = url.host()->serialize();
        else
            hostname = String {};

        // 8. Set port to url’s port, serialized, or the empty string if the value is null.
        if (url.port().has_value())
            port = String::number(url.port().value());
        else
            port = String {};

        // 9. Set pathname to the result of URL path serializing url.
        pathname = url.serialize_path();

        // 10. Set search to url’s query or the empty string if the value is null.
        search = url.query().value_or(String {});

        // 11. Set hash to url’s fragment or the empty string if the value is null.
        hash = url.fragment().value_or(String {});
    }

    // 13. Let protocolExecResult be RegExpBuiltinExec(urlPattern’s protocol component's regular expression, protocol).
    auto protocol_exec_result = m_protocol_component.regular_expression->match(protocol);
    if (!protocol_exec_result.success)
        return OptionalNone {};

    // 14. Let usernameExecResult be RegExpBuiltinExec(urlPattern’s username component's regular expression, username).
    auto username_exec_result = m_username_component.regular_expression->match(username);
    if (!username_exec_result.success)
        return OptionalNone {};

    // 15. Let passwordExecResult be RegExpBuiltinExec(urlPattern’s password component's regular expression, password).
    auto password_exec_result = m_password_component.regular_expression->match(password);
    if (!password_exec_result.success)
        return OptionalNone {};

    // 16. Let hostnameExecResult be RegExpBuiltinExec(urlPattern’s hostname component's regular expression, hostname).
    auto hostname_exec_result = m_hostname_component.regular_expression->match(hostname);
    if (!hostname_exec_result.success)
        return OptionalNone {};

    // 17. Let portExecResult be RegExpBuiltinExec(urlPattern’s port component's regular expression, port).
    auto port_exec_result = m_port_component.regular_expression->match(port);
    if (!port_exec_result.success)
        return OptionalNone {};

    // 18. Let pathnameExecResult be RegExpBuiltinExec(urlPattern’s pathname component's regular expression, pathname).
    auto pathname_exec_result = m_pathname_component.regular_expression->match(pathname);
    if (!pathname_exec_result.success)
        return OptionalNone {};

    // 19. Let searchExecResult be RegExpBuiltinExec(urlPattern’s search component's regular expression, search).
    auto search_exec_result = m_search_component.regular_expression->match(search);
    if (!search_exec_result.success)
        return OptionalNone {};

    // 20. Let hashExecResult be RegExpBuiltinExec(urlPattern’s hash component's regular expression, hash).
    auto hash_exec_result = m_hash_component.regular_expression->match(hash);
    if (!hash_exec_result.success)
        return OptionalNone {};

    // 21. If protocolExecResult, usernameExecResult, passwordExecResult, hostnameExecResult, portExecResult,
    //     pathnameExecResult, searchExecResult, or hashExecResult are null then return null.
    // NOTE: Done in steps above at point of exec.

    // 22. Let result be a new URLPatternResult.
    URLPatternResult result;

    // 23. Set result["inputs"] to inputs.
    result.inputs = inputs;

    // 24. Set result["protocol"] to the result of creating a component match result given urlPattern’s protocol
    //     component, protocol, and protocolExecResult.
    result.protocol = create_a_component_match_result(m_protocol_component, protocol, protocol_exec_result);

    // 25. Set result["username"] to the result of creating a component match result given urlPattern’s username
    //     component, username, and usernameExecResult.
    result.username = create_a_component_match_result(m_username_component, username, username_exec_result);

    // 26. Set result["password"] to the result of creating a component match result given urlPattern’s password
    //     component, password, and passwordExecResult.
    result.password = create_a_component_match_result(m_password_component, password, password_exec_result);

    // 27. Set result["hostname"] to the result of creating a component match result given urlPattern’s hostname
    //     component, hostname, and hostnameExecResult.
    result.hostname = create_a_component_match_result(m_hostname_component, hostname, hostname_exec_result);

    // 28. Set result["port"] to the result of creating a component match result given urlPattern’s port component,
    //     port, and portExecResult.
    result.port = create_a_component_match_result(m_port_component, port, port_exec_result);

    // 29. Set result["pathname"] to the result of creating a component match result given urlPattern’s pathname
    //     component, pathname, and pathnameExecResult.
    result.pathname = create_a_component_match_result(m_pathname_component, pathname, pathname_exec_result);

    // 30. Set result["search"] to the result of creating a component match result given urlPattern’s search component,
    //     search, and searchExecResult.
    result.search = create_a_component_match_result(m_search_component, search, search_exec_result);

    // 31. Set result["hash"] to the result of creating a component match result given urlPattern’s hash component,
    //     hash, and hashExecResult.
    result.hash = create_a_component_match_result(m_hash_component, hash, hash_exec_result);

    // 32. Return result.
    return result;
}

// https://urlpattern.spec.whatwg.org/#dom-urlpattern-protocol
String const& URLPattern::protocol() const
{
    // 1. Return this's associated URL pattern's protocol component's pattern string.
    return m_url_pattern.protocol_component().pattern_string;
}

// https://urlpattern.spec.whatwg.org/#dom-urlpattern-username
String const& URLPattern::username() const
{
    // 1. Return this's associated URL pattern's username component's pattern string.
    return m_url_pattern.username_component().pattern_string;
}

// https://urlpattern.spec.whatwg.org/#dom-urlpattern-password
String const& URLPattern::password() const
{
    // 1. Return this's associated URL pattern's password component's pattern string.
    return m_url_pattern.password_component().pattern_string;
}

// https://urlpattern.spec.whatwg.org/#dom-urlpattern-hostname
String const& URLPattern::hostname() const
{
    // 1. Return this's associated URL pattern's hostname component's pattern string.
    return m_url_pattern.hostname_component().pattern_string;
}

// https://urlpattern.spec.whatwg.org/#dom-urlpattern-port
String const& URLPattern::port() const
{
    // 1. Return this's associated URL pattern's port component's pattern string.
    return m_url_pattern.port_component().pattern_string;
}

// https://urlpattern.spec.whatwg.org/#dom-urlpattern-pathname
String const& URLPattern::pathname() const
{
    // 1. Return this's associated URL pattern's pathname component's pattern string.
    return m_url_pattern.pathname_component().pattern_string;
}

// https://urlpattern.spec.whatwg.org/#dom-urlpattern-search
String const& URLPattern::search() const
{
    // 1. Return this's associated URL pattern's search component's pattern string.
    return m_url_pattern.search_component().pattern_string;
}

// https://urlpattern.spec.whatwg.org/#dom-urlpattern-hash
String const& URLPattern::hash() const
{
    // 1. Return this's associated URL pattern's hash component's pattern string.
    return m_url_pattern.hash_component().pattern_string;
}

// https://urlpattern.spec.whatwg.org/#dom-urlpattern-hasregexpgroups
bool URLPattern::has_reg_exp_groups() const
{
    // 1. If this's associated URL pattern's has regexp groups, then return true.
    if (m_url_pattern.has_regexp_groups())
        return true;

    // 2. Return false.
    return false;
}

// https://urlpattern.spec.whatwg.org/#dom-urlpattern-test
WebIDL::ExceptionOr<bool> URLPattern::test(URLPatternInput const& input, Optional<String> const& base_url) const
{
    // 1. Let result be the result of match given this's associated URL pattern, input, and baseURL if given.
    auto result = TRY(m_url_pattern.match(input, base_url));

    // 2. If result is null, return false.
    if (!result.has_value())
        return false;

    // 3. Return true.
    return true;
}

// https://urlpattern.spec.whatwg.org/#dom-urlpattern-exec
WebIDL::ExceptionOr<Optional<URLPatternResult>> URLPattern::exec(URLPatternInput const& input, Optional<String> const& base_url) const
{
    // 1. Return the result of match given this's associated URL pattern, input, and baseURL if given.
    return m_url_pattern.match(input, base_url);
}

// https://urlpattern.spec.whatwg.org/#url-pattern-has-regexp-groups
bool URLPatternRecord::has_regexp_groups() const
{
    // 1. If urlPattern’s protocol component has regexp groups is true, then return true.
    if (m_protocol_component.has_regexp_groups)
        return true;

    // 2. If urlPattern’s username component has regexp groups is true, then return true.
    if (m_username_component.has_regexp_groups)
        return true;

    // 3. If urlPattern’s password component has regexp groups is true, then return true.
    if (m_password_component.has_regexp_groups)
        return true;

    // 4. If urlPattern’s hostname component has regexp groups is true, then return true.
    if (m_hostname_component.has_regexp_groups)
        return true;

    // 5. If urlPattern’s port component has regexp groups is true, then return true.
    if (m_port_component.has_regexp_groups)
        return true;

    // 6. If urlPattern’s pathname component has regexp groups is true, then return true.
    if (m_pathname_component.has_regexp_groups)
        return true;

    // 7. If urlPattern’s search component has regexp groups is true, then return true.
    if (m_search_component.has_regexp_groups)
        return true;

    // 8. If urlPattern’s hash component has regexp groups is true, then return true.
    if (m_hash_component.has_regexp_groups)
        return true;

    // 9. Return false.
    return false;
}

URLPattern::~URLPattern() = default;

void URLPattern::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    WEB_SET_PROTOTYPE_FOR_INTERFACE(URLPattern);
}

}
