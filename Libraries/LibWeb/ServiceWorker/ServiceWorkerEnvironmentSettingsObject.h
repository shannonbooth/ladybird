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
    static GC::Ref<ServiceWorkerEnvironmentSettingsObject> setup(GC::Ref<Page> page,
        GC::Ref<ServiceWorkerGlobalScope> global_scope,
        URL::URL service_worker_script_url,
        NonnullOwnPtr<JS::ExecutionContext> execution_context,
        HighResolutionTime::DOMHighResTimeStamp unsafe_worker_creation_time);

    virtual ~ServiceWorkerEnvironmentSettingsObject() override = default;

    virtual GC::Ptr<DOM::Document> responsible_document() override;
    virtual String api_url_character_encoding() const override;
    virtual URL::URL api_base_url() const override;
    virtual URL::Origin origin() const override;
    virtual GC::Ref<HTML::PolicyContainer> policy_container() const override;
    virtual HTML::CanUseCrossOriginIsolatedAPIs cross_origin_isolated_capability() const override;
    virtual double time_origin() const override;

protected:
    ServiceWorkerEnvironmentSettingsObject(NonnullOwnPtr<JS::ExecutionContext> execution_context, GC::Ref<ServiceWorkerGlobalScope> global_scope, URL::URL service_worker_script_url, HighResolutionTime::DOMHighResTimeStamp unsafe_worker_creation_time)
        : EnvironmentSettingsObject(move(execution_context))
        , m_worker_global_scope(global_scope)
        , m_service_worker_script_url(move(service_worker_script_url))
        , m_unsafe_worker_creation_time(unsafe_worker_creation_time)
    {
    }

private:
    virtual void visit_edges(Visitor&) override;

    GC::Ref<ServiceWorkerGlobalScope> m_worker_global_scope;
    URL::URL m_service_worker_script_url;
    URL::Origin m_registering_service_worker_clients_origin;
    HighResolutionTime::DOMHighResTimeStamp m_unsafe_worker_creation_time { 0 };
};

}
