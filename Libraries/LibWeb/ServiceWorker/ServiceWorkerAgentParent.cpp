/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/PrincipalHostDefined.h>
#include <LibWeb/HTML/MessagePort.h>
#include <LibWeb/Page/Page.h>
#include <LibWeb/ServiceWorker/ServiceWorkerAgentParent.h>

namespace Web::ServiceWorker {

void ServiceWorkerAgentParent::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_message_port);
    visitor.visit(m_outside_port);
}

void ServiceWorkerAgentParent::initialize(JS::Realm& realm)
{
    Base::initialize(realm);

    m_message_port = HTML::MessagePort::create(realm);
    m_message_port->entangle_with(*m_outside_port);

    HTML::TransferDataHolder data_holder;
    MUST(m_message_port->transfer_steps(data_holder));

    // NOTE: This blocking IPC call may launch another process.
    //    If spinning the event loop for this can cause other javascript to execute, we're in trouble.
    auto worker_socket_file = Bindings::principal_host_defined_page(realm).client().request_worker_agent(Bindings::AgentType::ServiceWorker);

    auto worker_socket = MUST(Core::LocalSocket::adopt_fd(worker_socket_file.take_fd()));
    MUST(worker_socket->set_blocking(true));

    // TODO: Mach IPC
    auto transport = make<IPC::Transport>(move(worker_socket));

    m_worker_ipc = make_ref_counted<HTML::WebWorkerClient>(move(transport));
    m_worker_ipc->async_start_service_worker(URL::about_blank());
}

}
