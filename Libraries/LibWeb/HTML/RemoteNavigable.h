/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGC/CellAllocator.h>
#include <LibGC/Ptr.h>
#include <LibWeb/Export.h>
#include <LibWeb/Forward.h>
#include <LibWeb/HTML/Navigable.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/document-sequences.html#navigable
class WEB_API RemoteNavigable final : public Navigable {
    GC_CELL(RemoteNavigable, Navigable);
    GC_DECLARE_ALLOCATOR(RemoteNavigable);

public:
    explicit RemoteNavigable(String id, GC::Ptr<Navigable> parent = nullptr, String target_name = {});

    virtual String const& id() const override { return m_id; }
    virtual String target_name() const override { return m_target_name; }
    virtual GC::Ptr<Navigable> parent() const override { return m_parent; }
    void set_parent(GC::Ptr<Navigable> parent) { m_parent = parent; }
    void set_target_name(String target_name) { m_target_name = move(target_name); }

    virtual GC::Ptr<WindowProxy> active_window_proxy() override { return nullptr; }

private:
    virtual void visit_edges(Visitor&) override;

    String m_id;
    String m_target_name;
    GC::Ptr<Navigable> m_parent;
};

}
