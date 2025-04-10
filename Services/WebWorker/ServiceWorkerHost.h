/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/RefCounted.h>
#include <LibURL/URL.h>
#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/Bindings/WorkerPrototype.h>
#include <LibWeb/Forward.h>
#include <LibWeb/HTML/Scripting/SerializedEnvironmentSettingsObject.h>
#include <LibWeb/HTML/StructuredSerialize.h>

namespace WebWorker {

class ServiceWorkerHost : public RefCounted<ServiceWorkerHost> {
public:
    explicit ServiceWorkerHost(Web::ServiceWorker::SerializedServiceWorkerRecord);
    ~ServiceWorkerHost();

    void setup(GC::Ref<Web::Page>);

private:
    GC::Root<Web::HTML::WorkerDebugConsoleClient> m_console;

    Web::ServiceWorker::SerializedServiceWorkerRecord m_service_worker;
};

}
