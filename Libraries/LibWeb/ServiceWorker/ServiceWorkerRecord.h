/*
 * Copyright (c) 2024, Andrew Kaster <andrew@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibURL/URL.h>
#include <LibWeb/Bindings/ServiceWorkerPrototype.h>
#include <LibWeb/Bindings/WorkerPrototype.h>
#include <LibWeb/Forward.h>

namespace Web::ServiceWorker {

// https://w3c.github.io/ServiceWorker/#dfn-service-worker
// This class corresponds to "service worker", not "ServiceWorker"
// FIXME: This should be owned and managed at the user agent level
// FIXME: A lot of the fields for this struct actually need to live in the Agent for the service worker in the WebWorker process
struct ServiceWorkerRecord {
    // https://w3c.github.io/ServiceWorker/#dfn-state
    // A service worker has an associated state, which is one of "parsed", "installing", "installed", "activating", "activated", and "redundant". It is initially "parsed".
    Bindings::ServiceWorkerState state = Bindings::ServiceWorkerState::Parsed;

    // https://w3c.github.io/ServiceWorker/#dfn-script-url
    // A service worker has an associated script url (a URL).
    URL::URL script_url;

    // https://w3c.github.io/ServiceWorker/#dfn-type
    // A service worker has an associated type which is either "classic" or "module". Unless stated otherwise, it is "classic".
    Bindings::WorkerType worker_type = Bindings::WorkerType::Classic;

    // https://w3c.github.io/ServiceWorker/#dfn-classic-scripts-imported-flag
    // A service worker has an associated classic scripts imported flag. It is initially unset.
    bool classic_scripts_imported { false };

    // https://w3c.github.io/ServiceWorker/#dfn-script-resource
    // A service worker has an associated script resource (a script), which represents its own script resource. It is initially set to null.
    // FIXME: This cannot be a JS object for this to live at the user agent level (due to the serialization requirement).
    GC::Root<HTML::Script> script_resource;

    // https://w3c.github.io/ServiceWorker/#dfn-script-resource-map
    // A service worker has an associated script resource map which is an ordered map where the keys are URLs and the values are responses.
    OrderedHashMap<URL::URL, GC::Root<Fetch::Infrastructure::Response>> script_resource_map;

    // https://w3c.github.io/ServiceWorker/#dfn-set-of-used-scripts
    // A service worker has an associated set of used scripts (a set) whose item is a URL. It is initially a new set.
    Vector<URL::URL> set_of_used_scripts;

    // https://w3c.github.io/ServiceWorker/#service-worker-running
    bool is_running() const
    {
        // FIXME: A service worker is said to be running if its event loop is running.
        return false;
    }

    // https://w3c.github.io/ServiceWorker/#service-worker-start-status
    // A service worker has an associated start status which can be null or a Completion. It is initially null.
    Optional<JS::Completion> start_status;

    // https://w3c.github.io/ServiceWorker/#dfn-service-worker-global-object
    // A service worker has an associated global object (a ServiceWorkerGlobalScope object or null).
    GC::Root<ServiceWorkerAgentParent> global_object;

    // FIXME: A lot more fields after this...
};

// https://w3c.github.io/ServiceWorker/#dfn-service-worker
// This is equivalent to ServiceWorkerRecord above, but only contains the fields that are needed to be serialized over IPC.
struct SerializedServiceWorkerRecord {
    URL::URL script_url;
};

}

namespace IPC {

template<>
ErrorOr<void> encode(Encoder&, Web::ServiceWorker::SerializedServiceWorkerRecord const&);

template<>
ErrorOr<Web::ServiceWorker::SerializedServiceWorkerRecord> decode(Decoder&);

}
