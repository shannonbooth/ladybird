/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGC/Ptr.h>
#include <LibJS/Heap/Cell.h>
#include <LibWeb/Export.h>
#include <LibWeb/Forward.h>

namespace Web::HTML {

class WEB_API Navigable : public JS::Cell {
    GC_CELL(Navigable, JS::Cell);

public:
    virtual GC::Ptr<Navigable> parent_navigable() const = 0;
    virtual GC::Ptr<WindowProxy> active_window_proxy() = 0;
};

}
