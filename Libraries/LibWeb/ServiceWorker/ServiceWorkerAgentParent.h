/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGC/CellAllocator.h>
#include <LibJS/Heap/Cell.h>
#include <LibWeb/Forward.h>
#include <LibWeb/Worker/WebWorkerClient.h>

namespace Web::ServiceWorker {

class ServiceWorkerAgentParent : public JS::Cell {
    GC_CELL(WorkerAgent, JS::Cell);
    GC_DECLARE_ALLOCATOR(ServiceWorkerAgentParent);

protected:
    ServiceWorkerAgentParent() = default;

private:
    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Visitor&) override;

    GC::Ptr<HTML::MessagePort> m_message_port;
    GC::Ptr<HTML::MessagePort> m_outside_port;
    RefPtr<Web::HTML::WebWorkerClient> m_worker_ipc;
};

}
