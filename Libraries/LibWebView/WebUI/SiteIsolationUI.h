/*
 * Copyright (c) 2026, Ladybird contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWebView/Forward.h>
#include <LibWebView/WebUI.h>

namespace WebView {

class WEBVIEW_API SiteIsolationUI : public WebUI {
    WEB_UI(SiteIsolationUI);

private:
    virtual void register_interfaces() override;

    void update_site_isolation_state();
};

}
