/*
 * Copyright (c) 2025, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibURL/Forward.h>
#include <LibWebView/Forward.h>

namespace WebView {

enum class NavigationTarget {
    TopLevel,
    IFrame,
};

WEBVIEW_API void enable_site_isolation();
WEBVIEW_API void disable_site_isolation();
WEBVIEW_API void enable_iframe_site_isolation();
WEBVIEW_API void disable_iframe_site_isolation();
[[nodiscard]] WEBVIEW_API bool is_url_suitable_for_same_process_navigation(URL::URL const& current_url, URL::URL const& target_url, NavigationTarget = NavigationTarget::TopLevel);

}
