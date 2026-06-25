/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <LibGC/Ptr.h>
#include <LibJS/Heap/Cell.h>
#include <LibWeb/Export.h>
#include <LibWeb/Forward.h>

namespace Web::HTML {

class WEB_API Navigable : public JS::Cell {
    GC_CELL(Navigable, JS::Cell);

public:
    virtual String const& id() const = 0;
    virtual String target_name() const = 0;
    virtual GC::Ptr<Navigable> parent() const = 0;
    virtual GC::Ptr<WindowProxy> active_window_proxy() = 0;
};

}
