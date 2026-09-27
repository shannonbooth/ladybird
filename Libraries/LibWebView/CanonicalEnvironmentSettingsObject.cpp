/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/CanonicalEnvironmentSettingsObject.h>

namespace WebView {

CanonicalEnvironmentSettingsObject::CanonicalEnvironmentSettingsObject(Utf16String id)
    : m_id(move(id))
{
}

}
