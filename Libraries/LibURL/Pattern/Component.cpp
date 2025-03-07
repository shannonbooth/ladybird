/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibURL/Pattern/Component.h>

namespace URL::Pattern {

// https://urlpattern.spec.whatwg.org/#escape-a-regexp-string
String escape_a_regexp_string(String const& input)
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

}
