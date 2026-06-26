/*
 * Copyright (c) 2020-2023, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2021-2022, Linus Groh <linusg@serenityos.org>
 * Copyright (c) 2023, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/Compositor/CompositorHost.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/BrowsingContextGroup.h>
#include <LibWeb/HTML/DocumentState.h>
#include <LibWeb/HTML/LocalNavigable.h>
#include <LibWeb/HTML/LocalTraversableNavigable.h>
#include <WebContent/ConnectionFromClient.h>
#include <WebContent/PageClient.h>
#include <WebContent/PageHost.h>
#include <WebContent/WebContentCompositorHost.h>
#include <WebContent/WebDriverConnection.h>

namespace WebContent {

PageHost::PageHost(ConnectionFromClient& client)
    : m_client(client)
{
}

void PageHost::initialize(u64 initial_page_id)
{
    VERIFY(m_pages.is_empty());
    auto& first_page = create_page(initial_page_id);
    Web::HTML::LocalTraversableNavigable::create_a_fresh_top_level_traversable(first_page.page(), URL::about_blank());
}

void PageHost::initialize_embedded_frame(u64 initial_page_id, String local_navigable_id, Vector<Web::HTML::RemoteNavigableDescriptor> ancestors)
{
    VERIFY(m_pages.is_empty());
    auto& first_page = create_page(initial_page_id);
    auto& page = first_page.page();

    auto [group, document] = Web::HTML::BrowsingContextGroup::create_a_new_browsing_context_group_and_document(page);
    (void)group;

    auto document_state = Web::HTML::DocumentState::create();
    document_state->set_origin(document->origin());
    document_state->set_about_base_url(document->about_base_url());

    auto local_root = Web::HTML::LocalTraversableNavigable::create_for_embedded_frame_process_root(page, document_state, document, move(local_navigable_id));
    page.set_local_root_navigable(local_root);

    GC::Ptr<Web::HTML::Navigable> parent;
    JS::Realm& realm = document->realm();
    for (auto& ancestor : ancestors)
        parent = Web::HTML::Navigable::create_remote(realm, move(ancestor), parent);
    local_root->set_parent(parent);
}

PageClient& PageHost::create_page(u64 page_id)
{
    VERIFY(page_id > 0);
    VERIFY(!m_pages.contains(page_id));
    m_pages.set(page_id, PageClient::create(Web::Bindings::main_thread_vm(), *this, page_id));
    return *m_pages.get(page_id).value();
}

void PageHost::remove_page(Badge<PageClient>, u64 page_id)
{
    m_pages.remove(page_id);
}

Optional<PageClient&> PageHost::page(u64 page_id)
{
    return m_pages.get(page_id).map([](auto& value) -> PageClient& {
        return *value;
    });
}

PageHost::~PageHost() = default;

void PageHost::ensure_compositor_host()
{
    if (m_compositor_host)
        return;
    m_compositor_host = create_web_content_compositor_host(m_client);
}

void PageHost::compositor_process_reconnected()
{
    for (auto& [_, page] : m_pages)
        page->compositor_process_reconnected();
}

void PageHost::compositor_process_lost()
{
    for (auto& [_, page] : m_pages)
        page->compositor_process_lost();
}

}
