/*
 * Copyright (c) 2022, Ben Abraham <ben.d.abraham@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Vector.h>
#include <LibURL/URL.h>
#include <LibWeb/Export.h>
#include <LibWeb/Forward.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Scripting/SerializedEnvironmentSettingsObject.h>

namespace Web::HTML {

class WEB_API WorkerEnvironmentSettingsObject final
    : public EnvironmentSettingsObject {
    GC_CELL(WorkerEnvironmentSettingsObject, EnvironmentSettingsObject);
    GC_DECLARE_ALLOCATOR(WorkerEnvironmentSettingsObject);

public:
    using Owner = Variant<SerializedDocument, SerializedWorkerGlobalScope>;

    WorkerEnvironmentSettingsObject(JS::ExecutionContext& execution_context, URL::Origin origin, bool outside_settings_has_cross_site_ancestor, HighResolutionTime::DOMHighResTimeStamp unsafe_worker_creation_time)
        : EnvironmentSettingsObject(execution_context)
        , m_origin(move(origin))
        , m_outside_settings_has_cross_site_ancestor(outside_settings_has_cross_site_ancestor)
        , m_unsafe_worker_creation_time(unsafe_worker_creation_time)
    {
    }

    static GC::Ref<WorkerEnvironmentSettingsObject> setup(GC::Ref<Page> page, JS::Realm&, JS::ExecutionContext&, URL::URL const& worker_url, SerializedEnvironmentSettingsObject const& outside_settings, HighResolutionTime::DOMHighResTimeStamp unsafe_worker_creation_time);
    void set_global_scope(WorkerGlobalScope&);
    void append_owner(Owner owner) { m_owner_set.append(move(owner)); }
    Vector<Owner> const& owner_set() const { return m_owner_set; }

    virtual ~WorkerEnvironmentSettingsObject() override = default;

    virtual GC::Ptr<DOM::Document> responsible_document() override { return nullptr; }
    virtual URL::URL api_base_url() const override;
    virtual URL::Origin origin() const override;
    virtual bool has_cross_site_ancestor() const override;
    virtual GC::Ref<PolicyContainer> policy_container() const override;
    virtual CanUseCrossOriginIsolatedAPIs cross_origin_isolated_capability() const override;
    virtual double time_origin() const override;

private:
    virtual void visit_edges(JS::Cell::Visitor&) override;

    URL::Origin m_origin;
    bool m_outside_settings_has_cross_site_ancestor;

    GC::Ptr<WorkerGlobalScope> m_global_scope;
    Vector<Owner> m_owner_set;

    HighResolutionTime::DOMHighResTimeStamp m_unsafe_worker_creation_time { 0 };
};

}
