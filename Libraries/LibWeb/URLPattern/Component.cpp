/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibRegex/Regex.h>
#include <LibWeb/URLPattern/Component.h>
#include <LibWeb/URLPattern/PatternParser.h>
#include <LibWeb/URLPattern/PatternString.h>

namespace Web::URLPattern {

// https://urlpattern.spec.whatwg.org/#escape-a-regexp-string
static String escape_a_regexp_string(String const& input)
{
    // 1. Assert: input is an ASCII string.
    VERIFY(all_of(input.code_points(), is_ascii));

    // 2. Let result be the empty string.
    StringBuilder builder;

    // 3. Let index be 0.
    // 4. While index is less than input’s length:
    for (auto c : input.bytes_as_string_view()) {
        // 1. Let c be input[index].
        // 2. Increment index by 1.

        // 3. If c is one of:
        //     * U+002E (.);
        //     * U+002B (+);
        //     * U+002A (*);
        //     * U+003F (?);
        //     * U+005E (^);
        //     * U+0024 ($);
        //     * U+007B ({);
        //     * U+007D (});
        //     * U+0028 (();
        //     * U+0029 ());
        //     * U+005B ([);
        //     * U+005D (]);
        //     * U+007C (|);
        //     * U+002F (/); or
        //     * U+005C (\),
        //    then append "\" to the end of result.
        if (".+*?^${}()[]|/\\"sv.contains(c))
            builder.append('\\');

        // 4. Append c to the end of result.
        builder.append(c);
    }

    // 5. Return result.
    return builder.to_string_without_validation();
}

// https://urlpattern.spec.whatwg.org/#generate-a-segment-wildcard-regexp
String generate_a_segment_wildcard_regexp(PatternParser::Options const& options)
{
    // 1. Let result be "[^".
    StringBuilder result;
    result.append("[^"sv);

    // 2. Append the result of running escape a regexp string given options’s delimiter code point to the end of result.
    if (options.delimiter_code_point.has_value())
        result.append(escape_a_regexp_string(String::from_code_point(*options.delimiter_code_point)));

    // 3. Append "]+?" to the end of result.
    result.append("]+?"sv);

    // 4. Return result.
    return result.to_string_without_validation();
}

// https://urlpattern.spec.whatwg.org/#generate-a-regular-expression-and-name-list
struct RegularExpressionAndNameList {
    String regular_expression;
    Vector<String> name_list;
};

static RegularExpressionAndNameList generate_a_regular_expression_and_name_list(Vector<Part> const& part_list, PatternParser::Options const& options)
{
    // 1. Let result be "^".
    StringBuilder result;
    result.append('^');

    // 2. Let name list be a new list.
    Vector<String> name_list;

    // 3. For each part of part list:
    for (auto const& part : part_list) {
        // 1. If part’s type is "fixed-text":
        if (part.type == Part::Type::FixedText) {
            // 1. If part’s modifier is "none", then append the result of running escape a regexp string given part’s
            //    value to the end of result.
            if (part.modifier == Part::Modifier::None) {
                result.append(escape_a_regexp_string(part.value));
            }
            // 2. Otherwise:
            else {
                // 1. Append "(?:" to the end of result.
                result.append("(?:"sv);

                // 2. Append the result of running escape a regexp string given part’s value to the end of result.
                result.append(escape_a_regexp_string(part.value));

                // 3. Append ")" to the end of result.
                result.append(')');

                // 4. Append the result of running convert a modifier to a string given part’s modifier to the end of result.
                result.append(Part::convert_modifier_to_string(part.modifier));
            }

            // 3. Continue.
            continue;
        }

        // 2. Assert: part’s name is not the empty string.
        VERIFY(!part.name.is_empty());

        // 3. Append part’s name to name list.
        name_list.append(part.name);

        // 4. Let regexp value be part’s value.
        auto regexp_value = part.value;

        // 5. If part’s type is "segment-wildcard", then set regexp value to the result of running generate a segment wildcard regexp given options.
        if (part.type == Part::Type::SegmentWildcard) {
            regexp_value = generate_a_segment_wildcard_regexp(options);
        }
        // 6. Otherwise if part’s type is "full-wildcard", then set regexp value to full wildcard regexp value.
        else if (part.type == Part::Type::FullWildcard) {
            regexp_value = ".*"_string;
        }

        // 7. If part’s prefix is the empty string and part’s suffix is the empty string:
        if (part.prefix.is_empty() && part.suffix.is_empty()) {
            // 1. If part’s modifier is "none" or "optional", then:
            if (part.modifier == Part::Modifier::None || part.modifier == Part::Modifier::Optional) {
                // 1. Append "(" to the end of result.
                result.append('(');

                // 2. Append regexp value to the end of result.
                result.append(regexp_value);

                // 3. Append ")" to the end of result.
                result.append(')');

                // 4. Append the result of running convert a modifier to a string given part’s modifier to the end of result.
                result.append(Part::convert_modifier_to_string(part.modifier));
            }
            // 2. Otherwise:
            else {
                // 1. Append "((?:" to the end of result.
                result.append("((?:"sv);

                // 2. Append regexp value to the end of result.
                result.append(regexp_value);

                // 3. Append ")" to the end of result.
                result.append(')');

                // 4. Append the result of running convert a modifier to a string given part’s modifier to the end of result.
                result.append(Part::convert_modifier_to_string(part.modifier));

                // 5. Append ")" to the end of result.
                result.append(')');
            }

            // 3. Continue.
            continue;
        }

        // 8. If part’s modifier is "none" or "optional":
        if (part.modifier == Part::Modifier::None || part.modifier == Part::Modifier::Optional) {
            // 1. Append "(?:" to the end of result.
            result.append("(?:"sv);

            // 2. Append the result of running escape a regexp string given part’s prefix to the end of result.
            result.append(escape_a_regexp_string(part.prefix));

            // 3. Append "(" to the end of result.
            result.append('(');

            // 4. Append regexp value to the end of result.
            result.append(regexp_value);

            // 5. Append ")" to the end of result.
            result.append(')');

            // 6. Append the result of running escape a regexp string given part’s suffix to the end of result.
            result.append(escape_a_regexp_string(part.suffix));

            // 7. Append ")" to the end of result.
            result.append(')');

            // 8. Append the result of running convert a modifier to a string given part’s modifier to the end of result.
            result.append(Part::convert_modifier_to_string(part.modifier));

            // 9. Continue.
            continue;
        }

        // 9. Assert: part’s modifier is "zero-or-more" or "one-or-more".
        VERIFY(part.modifier == Part::Modifier::ZeroOrMore || part.modifier == Part::Modifier::OneOrMore);

        // 10. Assert: part’s prefix is not the empty string or part’s suffix is not the empty string.
        VERIFY(!part.prefix.is_empty() || !part.suffix.is_empty());

        // 11. Append "(?:" to the end of result.
        result.append("(?:"sv);

        // 12. Append the result of running escape a regexp string given part’s prefix to the end of result.
        result.append(escape_a_regexp_string(part.prefix));

        // 13. Append "((?:" to the end of result.
        result.append("((?:"sv);

        // 14. Append regexp value to the end of result.
        result.append(regexp_value);

        // 15. Append ")(?:" to the end of result.
        result.append(")(?:"sv);

        // 16. Append the result of running escape a regexp string given part’s suffix to the end of result.
        result.append(escape_a_regexp_string(part.suffix));

        // 17. Append the result of running escape a regexp string given part’s prefix to the end of result.
        result.append(escape_a_regexp_string(part.prefix));

        // 18. Append "(?:" to the end of result.
        result.append("(?:"sv);

        // 19. Append regexp value to the end of result.
        result.append(regexp_value);

        // 20. Append "))*)" to the end of result.
        result.append("))*)"sv);

        // 21. Append the result of running escape a regexp string given part’s suffix to the end of result.
        result.append(escape_a_regexp_string(part.suffix));

        // 22. Append ")" to the end of result.
        result.append(')');

        // 23. If part’s modifier is "zero-or-more" then append "?" to the end of result.
        if (part.modifier == Part::Modifier::ZeroOrMore)
            result.append('?');
    }

    // 4. Append "$" to the end of result.
    result.append('$');

    // 5. Return (result, name list).
    return { result.to_string_without_validation(), name_list };
}

// https://urlpattern.spec.whatwg.org/#compile-a-component
WebIDL::ExceptionOr<Component> Component::compile(Utf8View const& input, PatternParser::EncodingCallback encoding_callback,
    PatternParser::Options const& options)
{
    // 1. Let part list be the result of running parse a pattern string given input, options, and encoding callback.
    auto part_list = TRY(PatternParser::parse(input, options, move(encoding_callback)));

    // 2. Let (regular expression string, name list) be the result of running generate a regular expression and name
    //    list given part list and options.
    auto [regular_expression_string, name_list] = generate_a_regular_expression_and_name_list(part_list, options);

    // 3. Let flags be an empty string.
    auto flags = regex::RegexOptions<ECMAScriptFlags> {
        (regex::ECMAScriptFlags)regex::AllFlags::SingleMatch
        | (regex::ECMAScriptFlags)regex::AllFlags::Global
        | (regex::ECMAScriptFlags)regex::AllFlags::SkipTrimEmptyMatches
        | regex::ECMAScriptFlags::BrowserExtended
    };

    // 4. If options’s ignore case is true then set flags to "vi".
    if (options.ignore_case) {
        flags |= regex::ECMAScriptFlags::UnicodeSets;
        flags |= regex::ECMAScriptFlags::Insensitive;
    }
    // 5. Otherwise set flags to "v"
    else {
        flags |= regex::ECMAScriptFlags::UnicodeSets;
    }

    // 6. Let regular expression be RegExpCreate(regular expression string, flags). If this throws an exception, catch
    //    it, and throw a TypeError.
    Regex<ECMA262> regex(regular_expression_string.to_byte_string(), flags);
    if (regex.parser_result.error != regex::Error::NoError)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, MUST(String::formatted("RegExp compile error: {}", regex.error_string())) };

    // 7. Let pattern string be the result of running generate a pattern string given part list and options.
    auto pattern_string = generate_a_pattern_string(part_list, options);

    // 8. Let has regexp groups be false.
    bool has_regexp_groups = false;

    // 9. For each part of part list:
    for (auto const& part : part_list) {
        // 1. If part’s type is "regexp", then set has regexp groups to true.
        if (part.type == Part::Type::Regexp) {
            has_regexp_groups = true;
            break;
        }
    }

    // 10. Return a new component whose pattern string is pattern string, regular expression is regular expression,
    //     group name list is name list, and has regexp groups is has regexp groups.
    return Component {
        .pattern_string = pattern_string,
        .regular_expression = move(regex),
        .group_name_list = name_list,
        .has_regexp_groups = has_regexp_groups,
    };
}

}
