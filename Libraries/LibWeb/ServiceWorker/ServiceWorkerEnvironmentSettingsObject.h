/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibURL/URL.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/Forward.h>

namespace Web::ServiceWorker {

// The spec does not directly define this object, but this implements what is required by the step
// to set up a new environment settings object with the specified algorithms.
// https://w3c.github.io/ServiceWorker/#setup-serviceworkerglobalscope
class ServiceWorkerEnvironmentSettingsObject final
    : public HTML::EnvironmentSettingsObject {
    GC_CELL(ServiceWorkerEnvironmentSettingsObject, EnvironmentSettingsObject);
    GC_DECLARE_ALLOCATOR(ServiceWorkerEnvironmentSettingsObject);

public:
    ServiceWorkerEnvironmentSettingsObject(NonnullOwnPtr<JS::ExecutionContext> execution_context, GC::Ref<ServiceWorkerGlobalScope> worker_global_scope, HighResolutionTime::DOMHighResTimeStamp unsafe_worker_creation_time)
        : EnvironmentSettingsObject(move(execution_context))
        , m_worker_global_scope(worker_global_scope)
        , m_unsafe_worker_creation_time(unsafe_worker_creation_time)
    {
    }

    static GC::Ref<ServiceWorkerEnvironmentSettingsObject> setup(GC::Ref<Page> page, NonnullOwnPtr<JS::ExecutionContext> execution_context, HTML::SerializedEnvironmentSettingsObject const& outside_settings, HighResolutionTime::DOMHighResTimeStamp unsafe_worker_creation_time);

    virtual ~ServiceWorkerEnvironmentSettingsObject() override = default;

    virtual GC::Ptr<DOM::Document> responsible_document() override;
    virtual String api_url_character_encoding() const override;
    virtual URL::URL api_base_url() const override;
    virtual URL::Origin origin() const override;
    virtual GC::Ref<HTML::PolicyContainer> policy_container() const override;
    virtual HTML::CanUseCrossOriginIsolatedAPIs cross_origin_isolated_capability() const override;
    virtual double time_origin() const override;

private:
    virtual void visit_edges(Visitor&) override;

    // FIXME: Initialize this in the setup function.
    URL::Origin m_registering_service_worker_clients_origin;
    GC::Ref<ServiceWorkerGlobalScope> m_worker_global_scope;
    HighResolutionTime::DOMHighResTimeStamp m_unsafe_worker_creation_time { 0 };
};

}
