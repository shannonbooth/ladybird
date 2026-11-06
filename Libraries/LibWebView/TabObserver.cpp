/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/TabObserver.h>

namespace WebView {

TabObserver::TabObserver(ViewImplementation& tab)
    : m_tab(tab.make_weak_ptr())
{
    tab.add_observer({}, *this);
}

TabObserver::~TabObserver()
{
    if (auto* tab = m_tab.ptr())
        tab->remove_observer({}, *this);
}

}
