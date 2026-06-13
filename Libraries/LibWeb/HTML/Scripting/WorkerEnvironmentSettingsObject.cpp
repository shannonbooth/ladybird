/*
 * Copyright (c) 2023, Andrew Kaster <akaster@serenityos.org>
 * Copyright (c) 2024, Tim Ledbetter <timledbetter@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/PrincipalHostDefined.h>
#include <LibWeb/HTML/DedicatedWorkerGlobalScope.h>
#include <LibWeb/HTML/Scripting/Agent.h>
#include <LibWeb/HTML/Scripting/WorkerEnvironmentSettingsObject.h>
#include <LibWeb/HTML/WorkerGlobalScope.h>
#include <LibWeb/HighResolutionTime/TimeOrigin.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(WorkerEnvironmentSettingsObject);

// https://html.spec.whatwg.org/multipage/workers.html#set-up-a-worker-environment-settings-object
GC::Ref<WorkerEnvironmentSettingsObject> WorkerEnvironmentSettingsObject::setup(GC::Ref<Page> page, JS::Realm& realm, JS::ExecutionContext& execution_context, URL::URL const& worker_url, SerializedEnvironmentSettingsObject const& outside_settings, HighResolutionTime::DOMHighResTimeStamp unsafe_worker_creation_time)
{
    // 1. Let realm be the value of execution context's Realm component.
    VERIFY(execution_context.realm == &realm);

    // 2. Let origin be a unique opaque origin if workerURL's scheme is "data"; otherwise outside settings's origin.
    auto origin = worker_url.scheme() == "data" ? URL::Origin::create_opaque() : outside_settings.origin;

    // 3. Let settings object be a new environment settings object whose algorithms are defined as follows:
    // NOTE: See the functions defined for this class.
    // FIXME: Is it enough to cache the has_cross_site_ancestor of outside_settings, or do we need to check the live object somehow?
    auto settings_object = realm.create<WorkerEnvironmentSettingsObject>(execution_context, move(origin), outside_settings.has_cross_site_ancestor, unsafe_worker_creation_time);
    settings_object->target_browsing_context = nullptr;

    // FIXME: 4. Set settings object's id to a new unique opaque string, creation URL to workerURL, top-level creation URL to null, target browsing context to null, and active service worker to null.
    // NB: WorkerHost sets (ad-hoc) the global scope's url to be the worker URL before redirects, as the spec does not
    //     do so at that point. See https://github.com/whatwg/html/issues/11340.
    settings_object->creation_url = worker_url;

    // 5. If the worker global scope to be created is a DedicatedWorkerGlobalScope object, then set settings object's top-level origin to
    //    outside settings's top-level origin.
    // 6. Otherwise, set settings object's top-level origin to an implementation-defined value.
    // FIXME: We set this to the same top-level origin as DedicatedWorkerGlobalScope objects for now, as this needs
    //        to be non-null for determining network partition keys. The spec notes:
    //
    //        See Client-Side Storage Partitioning for the latest on properly defining this.
    //        https://privacycg.github.io/storage-partitioning/
    settings_object->top_level_origin = outside_settings.top_level_origin;

    // 7. Set realm's [[HostDefined]] field to settings object.
    auto intrinsics = realm.create<Bindings::Intrinsics>(realm);
    auto host_defined = make<Bindings::PrincipalHostDefined>(settings_object, intrinsics, page);
    realm.set_host_defined(move(host_defined));

    // 8. Return settings object.
    return settings_object;
}

void WorkerEnvironmentSettingsObject::set_global_scope(WorkerGlobalScope& global_scope)
{
    m_global_scope = global_scope;
    set_universal_global_scope(global_scope);
    register_with_responsible_event_loop(*relevant_agent(global_scope).event_loop);
}

// https://html.spec.whatwg.org/multipage/workers.html#script-settings-for-workers:api-base-url
URL::URL WorkerEnvironmentSettingsObject::api_base_url() const
{
    // Return worker global scope's url.
    return m_global_scope->url();
}

// https://html.spec.whatwg.org/multipage/workers.html#script-settings-for-workers:concept-settings-object-origin-2
URL::Origin WorkerEnvironmentSettingsObject::origin() const
{
    // Return origin.
    return m_origin;
}

// https://html.spec.whatwg.org/multipage/workers.html#script-settings-for-workers:concept-settings-object-has-cross-site-ancestor
bool WorkerEnvironmentSettingsObject::has_cross_site_ancestor() const
{
    // 1. If outside settings's has cross-site ancestor is true, then return true.
    if (m_outside_settings_has_cross_site_ancestor)
        return true;

    // 2. If worker global scope's url's scheme is "data", then return true.
    if (m_global_scope->url().scheme() == "data"sv)
        return true;

    // 3. Return false.
    return false;
}

// https://html.spec.whatwg.org/multipage/workers.html#script-settings-for-workers:concept-settings-object-policy-container
GC::Ref<PolicyContainer> WorkerEnvironmentSettingsObject::policy_container() const
{
    // Return worker global scope's policy container.
    return m_global_scope->policy_container();
}

// https://html.spec.whatwg.org/multipage/workers.html#script-settings-for-workers:concept-settings-object-cross-origin-isolated-capability
CanUseCrossOriginIsolatedAPIs WorkerEnvironmentSettingsObject::cross_origin_isolated_capability() const
{
    // FIXME: Return worker global scope's cross-origin isolated capability.
    return CanUseCrossOriginIsolatedAPIs::No;
}

// https://html.spec.whatwg.org/multipage/workers.html#script-settings-for-workers:concept-settings-object-time-origin
double WorkerEnvironmentSettingsObject::time_origin() const
{
    // Return the result of coarsening unsafeWorkerCreationTime with worker global scope's cross-origin isolated capability.
    return HighResolutionTime::coarsen_time(m_unsafe_worker_creation_time, cross_origin_isolated_capability());
}

void WorkerEnvironmentSettingsObject::visit_edges(JS::Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_global_scope);
}

}
