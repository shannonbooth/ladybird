/*
 * Copyright (c) 2026, Ladybird contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <AK/Vector.h>
#include <LibJS/Heap/Cell.h>
#include <LibURL/Origin.h>
#include <LibURL/URL.h>
#include <LibWeb/Export.h>
#include <LibWeb/Forward.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/document-sequences.html#navigable
class WEB_API Navigable : public JS::Cell {
    GC_CELL(Navigable, JS::Cell);
    GC_DECLARE_ALLOCATOR(Navigable);

public:
    struct ActiveDocumentMetadata {
        URL::URL url;
        URL::Origin origin { URL::Origin::create_opaque() };
    };

    Navigable() = default;
    virtual ~Navigable() override;

    String const& id() const { return m_id; }
    void set_id(String id) { m_id = move(id); }

    GC::Ptr<Navigable> parent() const { return m_parent; }
    void set_parent(GC::Ptr<Navigable>);

    Vector<GC::Root<Navigable>> child_navigables() const;
    void append_child_navigable(GC::Ref<Navigable>);
    void remove_child_navigable(Navigable&);

    GC::Ptr<WindowProxy> active_window_proxy() const { return m_active_window_proxy; }
    void set_active_window_proxy(GC::Ptr<WindowProxy> active_window_proxy) { m_active_window_proxy = active_window_proxy; }

    ActiveDocumentMetadata const& active_document_metadata() const { return m_active_document_metadata; }
    void set_active_document_metadata(ActiveDocumentMetadata metadata) { m_active_document_metadata = move(metadata); }

    String const& target_name() const { return m_target_name; }
    void set_target_name(String target_name) { m_target_name = move(target_name); }

    virtual GC::Ptr<LocalNavigable> local_navigable() { return nullptr; }
    virtual GC::Ptr<LocalNavigable const> local_navigable() const { return nullptr; }

protected:
    virtual void visit_edges(Cell::Visitor&) override;

private:
    String m_id;
    GC::Ptr<Navigable> m_parent;
    Vector<GC::Ref<Navigable>> m_child_navigables;
    GC::Ptr<WindowProxy> m_active_window_proxy;
    ActiveDocumentMetadata m_active_document_metadata;
    String m_target_name;
};

}
