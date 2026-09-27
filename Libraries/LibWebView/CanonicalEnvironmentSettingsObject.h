/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Utf16String.h>
#include <LibURL/Origin.h>
#include <LibWeb/StorageAPI/StorageKey.h>
#include <LibWebView/Export.h>
#include <LibWebView/Forward.h>

namespace WebView {

// https://html.spec.whatwg.org/multipage/webappapis.html#environment-settings-object
class WEBVIEW_API CanonicalEnvironmentSettingsObject {
public:
    AK_ALLOC_WITH_KMALLOC;

    CanonicalEnvironmentSettingsObject(CanonicalWindow&, Utf16String id);

    // https://html.spec.whatwg.org/multipage/webappapis.html#concept-environment-id
    Utf16String const& id() const { return m_id; }

    URL::Origin const& origin() const;

private:
    CanonicalWindow& m_window;
    Utf16String m_id;
};

WEBVIEW_API Optional<Web::StorageAPI::StorageKey> obtain_a_storage_key(CanonicalEnvironmentSettingsObject const&);

}
