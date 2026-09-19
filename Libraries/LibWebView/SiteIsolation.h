/*
 * Copyright (c) 2025, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/StringView.h>
#include <LibURL/Forward.h>
#include <LibWebView/Forward.h>

namespace WebView {

enum class SiteIsolationMode {
    Disabled,
    TopLevel,
    IFrame,
};

[[nodiscard]] WEBVIEW_API Optional<SiteIsolationMode> site_isolation_mode_from_string(StringView);
[[nodiscard]] WEBVIEW_API StringView site_isolation_mode_to_string(SiteIsolationMode);
[[nodiscard]] WEBVIEW_API SiteIsolationMode site_isolation_mode();
WEBVIEW_API void set_site_isolation_mode(SiteIsolationMode);

[[nodiscard]] WEBVIEW_API bool top_level_navigation_requires_process_swap(CanonicalBrowsingContext const&, URL::URL const& current_url, URL::URL const& target_url);

}
