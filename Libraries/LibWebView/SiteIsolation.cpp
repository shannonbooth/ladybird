/*
 * Copyright (c) 2025, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibURL/URL.h>
#include <LibWeb/Fetch/Infrastructure/URL.h>
#include <LibWeb/HTML/BrowsingContext.h>
#include <LibWebView/CanonicalBrowsingContext.h>
#include <LibWebView/CanonicalBrowsingContextGroup.h>
#include <LibWebView/SiteIsolation.h>

namespace WebView {

static SiteIsolationMode s_site_isolation_mode = SiteIsolationMode::TopLevel;

Optional<SiteIsolationMode> site_isolation_mode_from_string(StringView mode)
{
    if (mode.equals_ignoring_ascii_case("disable"sv) || mode.equals_ignoring_ascii_case("disabled"sv))
        return SiteIsolationMode::Disabled;
    if (mode.equals_ignoring_ascii_case("top-level"sv))
        return SiteIsolationMode::TopLevel;
    if (mode.equals_ignoring_ascii_case("iframe"sv) || mode.equals_ignoring_ascii_case("iframes"sv))
        return SiteIsolationMode::IFrame;
    return {};
}

StringView site_isolation_mode_to_string(SiteIsolationMode mode)
{
    switch (mode) {
    case SiteIsolationMode::Disabled:
        return "disable"sv;
    case SiteIsolationMode::TopLevel:
        return "top-level"sv;
    case SiteIsolationMode::IFrame:
        return "iframe"sv;
    }
    VERIFY_NOT_REACHED();
}

SiteIsolationMode site_isolation_mode()
{
    return s_site_isolation_mode;
}

void set_site_isolation_mode(SiteIsolationMode mode)
{
    s_site_isolation_mode = mode;
}

// Whether a top-level navigation from one URL to another leaves the tab's process behind.
bool top_level_navigation_requires_process_swap(CanonicalBrowsingContext const& browsing_context, URL::URL const& current_url, URL::URL const& target_url)
{
    if (site_isolation_mode() == SiteIsolationMode::Disabled)
        return false;

    // Obtaining a browsing context to use for a navigation response only lets an implementation-defined browsing
    // context group switch happen when the group holds a single browsing context (step 8). Ladybird cannot retain
    // WindowProxy relationships across a process swap either, so related top-level browsing contexts share a process.
    auto group = browsing_context.group();
    VERIFY(group);
    if (group->browsing_context_set().size() > 1)
        return false;

    // Allow navigating from about:blank to any site.
    if (Web::HTML::url_matches_about_blank(current_url))
        return false;

    // Make sure JavaScript URLs run in the same process.
    if (target_url.scheme() == "javascript"sv)
        return false;

    // Allow cross-scheme non-HTTP(S) navigation. Disallow cross-scheme HTTP(S) navigation.
    auto current_url_is_http = Web::Fetch::Infrastructure::is_http_or_https_scheme(current_url.scheme());
    auto target_url_is_http = Web::Fetch::Infrastructure::is_http_or_https_scheme(target_url.scheme());
    if (!current_url_is_http || !target_url_is_http)
        return current_url_is_http || target_url_is_http;

    return !current_url.origin().is_same_site(target_url.origin());
}

}
