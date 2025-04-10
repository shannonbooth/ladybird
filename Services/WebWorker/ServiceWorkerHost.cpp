/*
 * Copyright (c) 2023, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibIPC/File.h>
#include <LibJS/Runtime/ConsoleObject.h>
#include <LibWeb/Fetch/Fetching/Fetching.h>
#include <LibWeb/Fetch/Infrastructure/FetchAlgorithms.h>
#include <LibWeb/HTML/MessagePort.h>
#include <LibWeb/HTML/Scripting/ClassicScript.h>
#include <LibWeb/HTML/Scripting/EnvironmentSettingsSnapshot.h>
#include <LibWeb/HTML/Scripting/Fetching.h>
#include <LibWeb/HTML/Scripting/WorkerEnvironmentSettingsObject.h>
#include <LibWeb/HTML/WorkerDebugConsoleClient.h>
#include <LibWeb/HTML/WorkerGlobalScope.h>
#include <LibWeb/HighResolutionTime/TimeOrigin.h>
#include <LibWeb/Loader/ResourceLoader.h>
#include <LibWeb/ServiceWorker/ServiceWorkerEnvironmentSettingsObject.h>
#include <LibWeb/ServiceWorker/ServiceWorkerGlobalScope.h>
#include <WebWorker/ServiceWorkerHost.h>

namespace WebWorker {

ServiceWorkerHost::ServiceWorkerHost(Web::ServiceWorker::SerializedServiceWorkerRecord service_worker)
    : m_service_worker(move(service_worker))
{
}

ServiceWorkerHost::~ServiceWorkerHost() = default;

// https://w3c.github.io/ServiceWorker/#setup-serviceworkerglobalscope
// 8. Let agent be the result of obtaining a service worker agent, and run the following steps in that context:
void ServiceWorkerHost::setup(GC::Ref<Web::Page> page)
{
    auto& vm = Web::Bindings::main_thread_vm();

    // 1. Let realmExecutionContext be the result of creating a new realm given agent and the following customizations:
    GC::Ptr<Web::ServiceWorker::ServiceWorkerGlobalScope> worker_global_scope;
    auto realm_execution_context = Web::Bindings::create_a_new_javascript_realm(
        vm,
        [page, &vm, &worker_global_scope](JS::Realm& realm) {
            // For the global object, create a new ServiceWorkerGlobalScope object. Let workerGlobalScope be the created object.
            worker_global_scope = vm.heap().allocate<Web::ServiceWorker::ServiceWorkerGlobalScope>(realm, page);
            return worker_global_scope;
        },
        nullptr);

    auto& realm = *realm_execution_context->realm;

    // 2. Let settingsObject be a new environment settings object whose algorithms are defined as follows:
    //     The realm execution context
    //         Return realmExecutionContext.
    //     The module map
    //         Return workerGlobalScope’s module map.
    //     The API base URL
    //         Return serviceWorker’s script url.
    //     The origin
    //         Return its registering service worker client’s origin.
    //     The policy container
    //         Return workerGlobalScope’s policy container.
    //     The time origin
    //         Return the result of coarsening unsafeCreationTime given workerGlobalScope’s cross-origin isolated capability.
    auto unsafe_worker_creation_time = Web::HighResolutionTime::unsafe_shared_current_time();
    auto settings_object = Web::ServiceWorker::ServiceWorkerEnvironmentSettingsObject::setup(page, *worker_global_scope, m_service_worker.script_url, move(realm_execution_context), unsafe_worker_creation_time);

    // 3. Set settingsObject’s id to a new unique opaque string, creation URL to serviceWorker’s script url, top-level creation URL to
    //    null, top-level origin to an implementation-defined value, target browsing context to null, and active service worker to null.
    settings_object->id = String {}; // FIXME: Implement this!
    settings_object->creation_url = m_service_worker.script_url;
    settings_object->top_level_creation_url = OptionalNone {};
    settings_object->top_level_origin = URL::Origin {}; // FIXME: What makes sense as a top level origin?
    settings_object->target_browsing_context = nullptr;
    // FIXME: Set active service worker to null.

    // 4. Set workerGlobalScope’s url to serviceWorker’s script url.
    worker_global_scope->set_url(m_service_worker.script_url);

    // FIXME: 5. Set workerGlobalScope’s policy container to serviceWorker’s script resource’s policy container.

    // FIXME: 6. Set workerGlobalScope’s type to serviceWorker’s type.

    // 7. Create a new WorkerLocation object and associate it with workerGlobalScope.
    worker_global_scope->set_location(realm.create<Web::HTML::WorkerLocation>(*worker_global_scope));

    // FIXME: 8. If the run CSP initialization for a global object algorithm returns "Blocked" when executed upon workerGlobalScope, set setupFailed to true and abort these steps.

    // 9. Set globalObject to workerGlobalScope.
    realm.set_global_object(*worker_global_scope);

    // FIXME: Console!
}

}
