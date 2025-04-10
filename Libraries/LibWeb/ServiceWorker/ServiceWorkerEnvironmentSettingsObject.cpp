/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/HighResolutionTime/TimeOrigin.h>
#include <LibWeb/ServiceWorker/ServiceWorkerEnvironmentSettingsObject.h>
#include <LibWeb/ServiceWorker/ServiceWorkerGlobalScope.h>

namespace Web::ServiceWorker {

GC_DEFINE_ALLOCATOR(ServiceWorkerEnvironmentSettingsObject);

GC::Ref<ServiceWorkerEnvironmentSettingsObject> ServiceWorkerEnvironmentSettingsObject::setup(GC::Ref<Page> page,
    GC::Ref<ServiceWorkerGlobalScope> global_scope,
    URL::URL service_worker_script_url,
    NonnullOwnPtr<JS::ExecutionContext> execution_context,
    HighResolutionTime::DOMHighResTimeStamp unsafe_worker_creation_time)
{
    (void)page; // FIXME:
    auto& realm = *execution_context->realm;
    auto& heap = realm.heap();

    return heap.allocate<ServiceWorkerEnvironmentSettingsObject>(move(execution_context), global_scope, move(service_worker_script_url), unsafe_worker_creation_time);
}

GC::Ptr<DOM::Document> ServiceWorkerEnvironmentSettingsObject::responsible_document()
{
    return nullptr;
}

String ServiceWorkerEnvironmentSettingsObject::api_url_character_encoding() const
{
    // FIXME: The spec doesn't say what this should be.
    return {};
}

URL::URL ServiceWorkerEnvironmentSettingsObject::api_base_url() const
{
    // The API base URL
    //     Return serviceWorker’s script url.
    return m_service_worker_script_url;
}

URL::Origin ServiceWorkerEnvironmentSettingsObject::origin() const
{
    // The origin
    //     Return its registering service worker client’s origin.
    return m_registering_service_worker_clients_origin;
}

GC::Ref<HTML::PolicyContainer> ServiceWorkerEnvironmentSettingsObject::policy_container() const
{
    // The policy container
    //     Return workerGlobalScope’s policy container.
    return m_worker_global_scope->policy_container();
}

HTML::CanUseCrossOriginIsolatedAPIs ServiceWorkerEnvironmentSettingsObject::cross_origin_isolated_capability() const
{
    // FIXME: The spec doesn't say what this should be, but I assume it should be 'worker global scope's cross-origin isolated capability'?
    return HTML::CanUseCrossOriginIsolatedAPIs::No;
}

double ServiceWorkerEnvironmentSettingsObject::time_origin() const
{
    // The time origin
    //     Return the result of coarsening unsafeCreationTime given workerGlobalScope’s cross-origin isolated capability.
    return HighResolutionTime::coarsen_time(m_unsafe_worker_creation_time, cross_origin_isolated_capability() == HTML::CanUseCrossOriginIsolatedAPIs::Yes);
}

void ServiceWorkerEnvironmentSettingsObject::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_worker_global_scope);
}

};
