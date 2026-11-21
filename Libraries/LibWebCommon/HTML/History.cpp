/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 * Copyright (c) 2025, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebCommon/HTML/History.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/nav-history-apis.html#can-have-its-url-rewritten
bool can_have_its_url_rewritten(URL::URL const& document_url, URL::URL const& target_url)
{
    // 1. Let documentURL be document's URL.
    // NB: The caller passes documentURL, as the UI process holds no Document.

    // 2. If targetURL and documentURL differ in their scheme, username, password, host, or port components,
    //    then return false.
    if (target_url.scheme() != document_url.scheme()
        || target_url.username() != document_url.username()
        || target_url.password() != document_url.password()
        || target_url.host() != document_url.host()
        || target_url.port() != document_url.port())
        return false;

    // 3. If targetURL's scheme is an HTTP(S) scheme, then return true.
    //    (Differences in path, query, and fragment are allowed for http: and https: URLs.)
    if (target_url.scheme() == "http"sv || target_url.scheme() == "https"sv)
        return true;

    // 4. If targetURL's scheme is "file", then:
    //    (Differences in query and fragment are allowed for file: URLs.)
    if (target_url.scheme() == "file"sv) {
        // 1. If targetURL and documentURL differ in their path component, then return false.
        if (target_url.serialize_path() != document_url.serialize_path())
            return false;

        // 2. Return true.
        return true;
    }

    // 5. If targetURL and documentURL differ in their path component or query components, then return false.
    //    (Only differences in fragment are allowed for other types of URLs.)
    if (target_url.serialize_path() != document_url.serialize_path() || target_url.query() != document_url.query())
        return false;

    // 6. Return true.
    return true;
}

}
