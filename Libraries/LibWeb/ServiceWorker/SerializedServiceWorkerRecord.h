/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
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
// This class corresponds to "service worker", not "ServiceWorker", and is what is passed over IPC for setting up & running a service worker.
struct SerializedServiceWorkerRecord {
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

    // https://w3c.github.io/ServiceWorker/#dfn-set-of-used-scripts
    // A service worker has an associated set of used scripts (a set) whose item is a URL. It is initially a new set.
    Vector<URL::URL> set_of_used_scripts;
};

}
