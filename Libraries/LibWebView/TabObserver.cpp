/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/TabObserver.h>

namespace WebView {

TabObserver::TabObserver(ViewImplementation& tab)
{
    observe(tab);
}

TabObserver::~TabObserver()
{
    stop_observing();
}

void TabObserver::observe(ViewImplementation& tab)
{
    stop_observing();
    m_tab = tab.make_weak_ptr();
    tab.add_observer({}, *this);
}

void TabObserver::stop_observing()
{
    if (auto* tab = m_tab.ptr())
        tab->remove_observer({}, *this);
    m_tab = nullptr;
}

}
