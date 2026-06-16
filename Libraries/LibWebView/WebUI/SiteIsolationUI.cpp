/*
 * Copyright (c) 2026, Ladybird contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/WebContentClient.h>
#include <LibWebView/WebUI/SiteIsolationUI.h>

namespace WebView {

void SiteIsolationUI::register_interfaces()
{
    register_interface("updateSiteIsolationState"sv, [this](auto const&) {
        update_site_isolation_state();
    });
}

void SiteIsolationUI::update_site_isolation_state()
{
    async_send_message("loadSiteIsolationState"sv, WebContentClient::site_isolation_process_tree_for_testing());
}

}
