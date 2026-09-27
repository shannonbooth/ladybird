/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Utf16String.h>
#include <LibWebView/Export.h>

namespace WebView {

// https://html.spec.whatwg.org/multipage/webappapis.html#environment-settings-object
class WEBVIEW_API CanonicalEnvironmentSettingsObject {
public:
    AK_ALLOC_WITH_KMALLOC;

    explicit CanonicalEnvironmentSettingsObject(Utf16String id);

    // https://html.spec.whatwg.org/multipage/webappapis.html#concept-environment-id
    Utf16String const& id() const { return m_id; }

private:
    Utf16String m_id;
};

}
