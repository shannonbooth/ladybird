/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/CanonicalNavigable.h>

#include <LibWeb/HTML/HistoryOperation.h>
#include <LibWeb/Page/ViewportIsFullscreen.h>
#include <LibWebView/Application.h>
#include <LibWebView/BrowsingSession.h>
#include <LibWebView/CanonicalBrowsingContext.h>
#include <LibWebView/CanonicalBrowsingContextGroup.h>
#include <LibWebView/CanonicalDocument.h>
#include <LibWebView/CanonicalTraversable.h>
#include <LibWebView/CanonicalWindow.h>
#include <LibWebView/SiteIsolation.h>
#include <LibWebView/ViewImplementation.h>
#include <LibWebView/WebContentClient.h>

namespace WebView {

CanonicalNavigable::CanonicalNavigable(Web::HTML::CrossProcessId id, Optional<Web::HTML::CrossProcessId> parent_id, RefPtr<CanonicalDocument> container_document)
    : m_id(id)
    , m_parent_id(parent_id)
    , m_container_document(move(container_document))
{
}

RefPtr<WebContentPage> CanonicalNavigable::reporting_page() const
{
    if (!m_container_document)
        return nullptr;
    return m_container_document->page_created_in();
}

CanonicalDocument& CanonicalNavigable::active_document() const
{
    // A navigable's active document is its active session history entry's document.
    // NB: The document is made the navigable's active document when its session history entry is activated.
    VERIFY(m_active_document);
    return *m_active_document;
}

void CanonicalNavigable::set_active_document(NonnullRefPtr<CanonicalDocument> document)
{
    // The document this one replaces was unloaded where it was hosted before this one activated.
    if (m_active_document && m_active_document != document)
        m_active_document->set_unloaded();
    m_active_document = move(document);
    m_active_document->set_node_navigable({}, *this);
}

// The document the navigable's ongoing navigation or the history step being applied populates, until it activates.
RefPtr<CanonicalDocument> CanonicalNavigable::document_being_populated() const
{
    RefPtr<CanonicalDocument> document;
    if (m_ongoing_navigation.has_value() && m_ongoing_navigation->document)
        document = m_ongoing_navigation->document;
    else
        document = top_level_traversable().document_of_pending_history_job(*this);
    if (document == m_active_document)
        return nullptr;
    return document;
}

// The navigable's document that page hosts: the one it populates, or the active document.
RefPtr<CanonicalDocument> CanonicalNavigable::document_hosted_by(WebContentPage const& page) const
{
    if (auto document = document_being_populated(); document && document->host() == &page)
        return document;
    if (m_active_document && m_active_document->host() == &page)
        return m_active_document;
    return nullptr;
}

CanonicalBrowsingContext& CanonicalNavigable::active_browsing_context() const
{
    // A navigable's active browsing context is its active document's browsing context.
    return active_document().browsing_context();
}

// https://html.spec.whatwg.org/multipage/browsers.html#obtain-browsing-context-navigation
NonnullRefPtr<CanonicalBrowsingContext> CanonicalNavigable::obtain_a_browsing_context_to_use_for_a_navigation_response(Web::HTML::OpenerPolicyEnforcementResult const& coop_enforcement_result)
{
    // 1. Let browsingContext be navigationParams's navigable's active browsing context.
    NonnullRefPtr browsing_context = active_browsing_context();

    // 2. If browsingContext is not a top-level browsing context, then return browsingContext.
    if (!is_top_level_traversable())
        return browsing_context;

    // 3. Let coopEnforcementResult be navigationParams's COOP enforcement result.
    // 4. Let swapGroup be coopEnforcementResult's needs a browsing context group switch.
    auto swap_group = coop_enforcement_result.needs_a_browsing_context_group_switch;

    // NB: Steps 5-8 only affect swapGroup through optional choices. This implementation does not take them.

    // 9. If swapGroup is false, then:
    if (!swap_group) {
        // FIXME: 1. If coopEnforcementResult's would need a browsing context group switch due to report-only is true,
        //           set browsingContext's virtual browsing context group ID to a new unique identifier.

        // 2. Return browsingContext.
        return browsing_context;
    }

    // 10. Let newBrowsingContext be the first return value of creating a new top-level browsing context and document.
    // NB: The navigation response's document replaces that document before any process creates it.
    auto new_browsing_context = CanonicalBrowsingContext::create_a_new_top_level_browsing_context_and_document(URL::Origin::create_opaque(), {}).browsing_context;

    // 11. Let navigationCOOP be navigationParams's cross-origin opener policy.
    // FIXME: 12. If navigationCOOP's value is "same-origin-plus-COEP", then set newBrowsingContext's group's
    //            cross-origin isolation mode to either "logical" or "concrete". The choice of which is
    //            implementation-defined.

    // 13. Let sandboxFlags be a clone of navigationParams's final sandboxing flag set.
    // FIXME: 14. If sandboxFlags is not empty, then:
    //            1. Assert: navigationCOOP's value is "unsafe-none".
    //            2. Assert: newBrowsingContext's popup sandboxing flag set is empty.
    //            3. Set newBrowsingContext's popup sandboxing flag set to sandboxFlags.

    // 15. Return newBrowsingContext.
    return new_browsing_context;
}

// NB: Only a top-level browsing context has a group. Where the specification asks for the group of a child navigable's
//     browsing context, it is its top-level browsing context's group, the one of the traversable's active browsing
//     context.
static CanonicalBrowsingContextGroup& group_of(CanonicalNavigable const& navigable, CanonicalBrowsingContext const& browsing_context)
{
    auto group = browsing_context.group();
    if (!group)
        group = navigable.top_level_traversable().active_browsing_context().group();
    VERIFY(group);
    return *group;
}

// A document the process hosting the navigable created without the UI process choosing a page for it, as for a
// javascript: URL, with the agent obtained for its origin in the navigable's browsing context group.
NonnullRefPtr<CanonicalDocument> CanonicalNavigable::document_created_by_host(URL::Origin const& origin)
{
    auto& browsing_context = active_browsing_context();
    // FIXME: Pass the document's requestsOAC value once Origin-Agent-Cluster is implemented.
    auto window = CanonicalWindow::create(group_of(*this, browsing_context).obtain_similar_origin_window_agent(origin, false));
    return CanonicalDocument::create(origin, browsing_context, move(window), CanonicalDocument::IsInitialAboutBlank::No);
}

// https://html.spec.whatwg.org/multipage/document-lifecycle.html#initialise-the-document-object
// NB: The steps choosing the browsing context, the Window and the agent that the Document for a navigation response is
//     created with. The process hosting that agent creates the Document and runs the other steps.
NonnullRefPtr<CanonicalDocument> CanonicalNavigable::create_and_initialize_a_document(NavigationLoader::ResponseDocument const& navigation_params)
{
    // 1. Let browsingContext be the result of obtaining a browsing context to use for a navigation response given navigationParams.
    auto browsing_context = obtain_a_browsing_context_to_use_for_a_navigation_response(navigation_params.coop_enforcement_result);

    // 5. Let window be null.
    RefPtr<CanonicalWindow> window;

    // 6. If browsingContext's active document's is initial about:blank is true, and browsingContext's active document's
    //    origin is same origin-domain with navigationParams's origin, then set window to browsingContext's active window.
    // NB: A browsing context created by a browsing context group switch is created with an initial about:blank that
    //     nothing here holds, whose opaque origin is not same origin-domain with navigationParams's origin. The
    //     initial about:blank's document.domain is in the process hosting it: an initial about:blank that set its own
    //     is same origin-domain with no other origin, and the origins are compared as same origin here.
    auto browsing_context_active_document = browsing_context->active_document();
    if (browsing_context_active_document && browsing_context_active_document->is_initial_about_blank()
        && browsing_context_active_document->origin().is_same_origin(navigation_params.origin)) {
        window = browsing_context->active_window();
    }
    // 7. Otherwise:
    else {
        // FIXME: 1. Let oacHeader be the result of getting a structured field value given `Origin-Agent-Cluster` and "item"
        //           from navigationParams's response's header list.
        // FIXME: 2. Let requestsOAC be true if oacHeader is not null and oacHeader[0] is the boolean true; otherwise false.
        // FIXME: 3. If navigationParams's reserved environment is a non-secure context, then set requestsOAC to false.
        auto requests_oac = false;

        // 4. Let agent be the result of obtaining a similar-origin window agent given navigationParams's origin,
        //    browsingContext's group, and requestsOAC.
        auto agent = group_of(*this, browsing_context).obtain_similar_origin_window_agent(navigation_params.origin, requests_oac);

        // 5. Let realmExecutionContext be the result of creating a new realm given agent and the following customizations:
        //    - For the global object, create a new Window object.
        //    - For the global this binding, use browsingContext's WindowProxy object.
        // 6. Set window to the global object of realmExecutionContext's Realm component.
        // NB: The realm is in the process hosting agent, which runs steps 7 to 12.
        window = CanonicalWindow::create(agent);
    }

    // 9. Let document be a new Document, with
    //    origin: navigationParams's origin
    //    browsing context: browsingContext
    // NB: The process hosting window's agent creates the document, with its other fields, and runs the remaining steps.
    // 22. Return document.
    return CanonicalDocument::create(navigation_params.origin, browsing_context, window.release_nonnull(), CanonicalDocument::IsInitialAboutBlank::No);
}

// NB: Which process hosts an agent is implementation-defined, and is where the documents of the agent are created. A
//     process hosting an agent keeps hosting it. A null process is a new one.
RefPtr<WebContentClient> CanonicalNavigable::obtain_process_to_host(CanonicalDocument const& document, Optional<URL::Origin> const& initiator_origin) const
{
    auto process_hosting_active_document = [&] -> WebContentClient& {
        auto page = top_level_traversable().page_hosting(*this);
        VERIFY(page);
        return page->client();
    };

    // A process creates the documents of the browsing contexts it holds. A browsing context created by a browsing
    // context group switch is created with its document in a new process.
    if (&document.browsing_context() != &active_browsing_context())
        return nullptr;

    // Without site isolation, the process hosting the active document hosts the next one. With site isolation of
    // top-level traversables, so does the process holding a child navigable's container.
    if (site_isolation_mode() == SiteIsolationMode::Disabled || (site_isolation_mode() == SiteIsolationMode::TopLevel && parent()))
        return process_hosting_active_document();

    if (auto process = document.relevant_global_object().agent().hosting_process())
        return process;

    // Nothing can address an opaque origin but the documents it was created from, so its agent is hosted where the
    // agent of the navigation's initiator origin is. When no process hosts that agent, as when the navigation was
    // not initiated by a document, the process hosting the navigable's active document does.
    if (document.origin().is_opaque()) {
        if (initiator_origin.has_value()) {
            if (auto process = group_of(*this, document.browsing_context()).obtain_similar_origin_window_agent(*initiator_origin, false)->hosting_process())
                return process;
        }
        return process_hosting_active_document();
    }

    // A process displaying a tab's initial about:blank that no document created, and hosting no other agent cluster,
    // is not yet used for any site. A child navigable's initial about:blank is in the process hosting its container's
    // document instead.
    auto& active_document = this->active_document();
    if (!parent() && active_document.is_initial_about_blank() && active_document.origin().is_opaque()) {
        auto& process = process_hosting_active_document();
        auto agent_cluster = active_document.relevant_global_object().agent().agent_cluster();
        if (agent_cluster && process.hosts_only_agent_cluster(*agent_cluster))
            return process;
    }
    return nullptr;
}

// The page of the process to create a child navigable's next document in, or of a new process when that is null.
ErrorOr<NonnullRefPtr<WebContentPage>> CanonicalNavigable::obtain_page_to_host_document_in(CanonicalDocument& document, RefPtr<WebContentClient> process)
{
    VERIFY(parent());
    auto& traversable = top_level_traversable();
    auto current_step = traversable.session_history().current_step();
    VERIFY(current_step.has_value());
    auto const* current_entry = traversable.session_history().get_the_target_history_entry(*this, *current_step);
    VERIFY(current_entry);

    RefPtr<WebContentPage> page;
    // The host takes the navigable's node over once the document it is to display is activated; until then, the page
    // hosting the displayed document keeps it.
    if (process && process == &reporting_page()->client()) {
        // The page holding the container populates the document in a provisional navigable while another page hosts
        // the displayed document.
        if (has_remote_host())
            process->async_begin_hosting_navigable(reporting_page()->id(), id(), *current_entry, traversable.system_visibility_state());
        page = reporting_page();
    } else if (process && has_remote_host() && process == &remote_host().client()) {
        page = remote_host();
    } else {
        // A process holds one page per tab, with the tab's whole graph: the process displaying the tab hosts a document
        // in the view's page, another process in the page it has for the tab, or in a page created for it.
        Compositing::PageId page_id;
        if (process && process->page_id_for_traversable(traversable).has_value()) {
            page_id = *process->page_id_for_traversable(traversable);
            process->async_begin_hosting_navigable(page_id, id(), *current_entry, traversable.system_visibility_state());
        } else if (process) {
            page_id = Application::the().allocate_page_id();
            process->async_create_embedded_page(page_id, traversable.remote_navigable_graph(), id(), *current_entry, traversable.system_visibility_state());
            process->register_embedded_page(page_id, traversable);
            traversable.represent_related_tabs_in(*process);
        } else {
            auto new_process = TRY(Application::the().launch_child_frame_web_content_process(reporting_page()->client().is_private(), traversable.remote_navigable_graph(), id(), *current_entry));
            process = move(new_process.client);
            page_id = new_process.page_id;
            process->register_embedded_page(page_id, traversable);
            traversable.represent_related_tabs_in(*process);
        }
        process->async_update_visibility_state(page_id, id(), traversable.system_visibility_state());
        page = process->page(page_id);
    }

    document.set_host(*page);
    send_viewport_to(*page);
    return page.release_nonnull();
}

CanonicalNavigable::~CanonicalNavigable()
{
    // The traversable, which removal and the view ask to discard a navigation's host, is gone before this runs.
    clear_ongoing_navigation_state();
}

bool CanonicalNavigable::is_hosted_by(WebContentPage const& page) const
{
    if (is_top_level_traversable())
        return false;
    return (has_remote_host() ? active_document().host() : reporting_page()).ptr() == &page;
}

void CanonicalNavigable::stage_same_document_session_history_entry(Web::HTML::CrossProcessId operation_id, Web::HTML::SameDocumentNavigationEntry entry)
{
    m_pending_same_document_session_history_entries.append({ operation_id, move(entry) });
}

Optional<Web::HTML::SameDocumentNavigationEntry> CanonicalNavigable::take_pending_same_document_session_history_entry(Web::HTML::CrossProcessId operation_id, Web::HTML::SessionHistoryEntryIdentity const& entry_identity)
{
    for (size_t i = 0; i < m_pending_same_document_session_history_entries.size(); ++i) {
        auto const& pending_entry = m_pending_same_document_session_history_entries[i];
        if (pending_entry.operation_id == operation_id
            && Web::HTML::session_history_entry_identity(pending_entry.entry) == entry_identity)
            return m_pending_same_document_session_history_entries.take(i).entry;
    }
    return {};
}

bool CanonicalNavigable::update_pending_same_document_session_history_entry(Web::HTML::SessionHistoryEntryIdentity const& entry_identity, Function<void(Web::HTML::SameDocumentNavigationEntry&)> const& update_entry)
{
    for (auto& pending_entry : m_pending_same_document_session_history_entries.in_reverse()) {
        if (Web::HTML::session_history_entry_identity(pending_entry.entry) != entry_identity)
            continue;
        update_entry(pending_entry.entry);
        return true;
    }
    return false;
}

bool CanonicalNavigable::has_pending_same_document_session_history_entry(Web::HTML::SessionHistoryEntryIdentity const& entry_identity) const
{
    for (auto const& pending_entry : m_pending_same_document_session_history_entries) {
        if (Web::HTML::session_history_entry_identity(pending_entry.entry) == entry_identity)
            return true;
    }
    return false;
}

void CanonicalNavigable::remove_pending_same_document_session_history_entries(Web::HTML::CrossProcessId operation_id)
{
    m_pending_same_document_session_history_entries.remove_all_matching([&](auto const& pending_entry) {
        return pending_entry.operation_id == operation_id;
    });
}

Vector<CanonicalNavigable::PendingSameDocumentSessionHistoryEntry> CanonicalNavigable::take_pending_same_document_session_history_entries()
{
    return move(m_pending_same_document_session_history_entries);
}

void CanonicalNavigable::append_pending_same_document_session_history_entries(Vector<PendingSameDocumentSessionHistoryEntry> entries)
{
    m_pending_same_document_session_history_entries.extend(move(entries));
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#nav-top
CanonicalTraversable& CanonicalNavigable::top_level_traversable()
{
    // 1. Let navigable be inputNavigable.
    auto* navigable = this;

    // 2. While navigable's parent is not null, set navigable to navigable's parent.
    while (navigable->parent())
        navigable = navigable->parent();

    // 3. Return navigable.
    VERIFY(navigable->is_top_level_traversable());
    return static_cast<CanonicalTraversable&>(*navigable);
}

CanonicalTraversable const& CanonicalNavigable::top_level_traversable() const
{
    return const_cast<CanonicalNavigable&>(*this).top_level_traversable();
}

CanonicalNavigable& CanonicalNavigable::append_child(NonnullOwnPtr<CanonicalNavigable> child)
{
    VERIFY(!child->m_parent);
    child->m_parent = this;
    m_children.append(move(child));
    return *m_children.last();
}

NonnullOwnPtr<CanonicalNavigable> CanonicalNavigable::remove_child(CanonicalNavigable& child)
{
    for (size_t i = 0; i < m_children.size(); ++i) {
        if (m_children[i].ptr() != &child)
            continue;

        auto removed_child = m_children.take(i);
        VERIFY(removed_child->m_parent == this);
        removed_child->m_parent = nullptr;
        return removed_child;
    }

    VERIFY_NOT_REACHED();
}

bool CanonicalNavigable::is_ancestor_of(CanonicalNavigable const& potential_descendant) const
{
    for (auto const* parent = potential_descendant.parent(); parent; parent = parent->parent()) {
        if (parent == this)
            return true;
    }
    return false;
}

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#allowed-to-navigate
bool CanonicalNavigable::allowed_by_sandboxing_to_navigate(CanonicalNavigable const& target, Web::InitiatorSourceSnapshot const& source_snapshot_params) const
{
    auto const& source = *this;

    // 1. If source is target, then return true.
    if (&source == &target)
        return true;

    // 2. If source is an ancestor of target, then return true.
    if (source.is_ancestor_of(target))
        return true;

    // 3. If target is an ancestor of source, then:
    if (target.is_ancestor_of(source)) {
        // 1. If target is not a top-level traversable, then return true.
        if (!target.is_top_level_traversable())
            return true;

        // 2. If sourceSnapshotParams's has transient activation is true, and sourceSnapshotParams's sandboxing flags's
        //    sandboxed top-level navigation with user activation browsing context flag is set, then return false.
        if (source_snapshot_params.has_transient_activation
            && has_flag(source_snapshot_params.sandboxing_flags, Web::HTML::SandboxingFlagSet::SandboxedTopLevelNavigationWithUserActivation)) {
            return false;
        }

        // 3. If sourceSnapshotParams's has transient activation is false, and sourceSnapshotParams's sandboxing flags's
        //    sandboxed top-level navigation without user activation browsing context flag is set, then return false.
        if (!source_snapshot_params.has_transient_activation
            && has_flag(source_snapshot_params.sandboxing_flags, Web::HTML::SandboxingFlagSet::SandboxedTopLevelNavigationWithoutUserActivation)) {
            return false;
        }

        // 4. Return true.
        return true;
    }

    // 4. If target is a top-level traversable:
    if (target.is_top_level_traversable()) {
        // FIXME: 1. If source is the one permitted sandboxed navigator of target, then return true.

        // 2. If sourceSnapshotParams's sandboxing flags's sandboxed navigation browsing context flag is set, then return false.
        if (has_flag(source_snapshot_params.sandboxing_flags, Web::HTML::SandboxingFlagSet::SandboxedNavigation))
            return false;

        // 3. Return true.
        return true;
    }

    // 5. If sourceSnapshotParams's sandboxing flags's sandboxed navigation browsing context flag is set, then return false.
    if (has_flag(source_snapshot_params.sandboxing_flags, Web::HTML::SandboxingFlagSet::SandboxedNavigation))
        return false;

    // 6. Return true.
    return true;
}

IterationDecision CanonicalNavigable::for_each_in_inclusive_subtree(Function<IterationDecision(CanonicalNavigable&)> const& callback)
{
    if (callback(*this) == IterationDecision::Break)
        return IterationDecision::Break;

    return for_each_in_subtree(callback);
}

IterationDecision CanonicalNavigable::for_each_in_subtree(Function<IterationDecision(CanonicalNavigable&)> const& callback)
{
    for (auto const& child : m_children) {
        if (child->for_each_in_inclusive_subtree(callback) == IterationDecision::Break)
            return IterationDecision::Break;
    }

    return IterationDecision::Continue;
}

IterationDecision CanonicalNavigable::for_each_in_inclusive_subtree(Function<IterationDecision(CanonicalNavigable const&)> const& callback) const
{
    if (callback(*this) == IterationDecision::Break)
        return IterationDecision::Break;

    return for_each_in_subtree(callback);
}

IterationDecision CanonicalNavigable::for_each_in_subtree(Function<IterationDecision(CanonicalNavigable const&)> const& callback) const
{
    for (auto const& child : m_children) {
        if (child->for_each_in_inclusive_subtree(callback) == IterationDecision::Break)
            return IterationDecision::Break;
    }

    return IterationDecision::Continue;
}

// A child navigable's active document is hosted by the page holding its container, or by another page.
bool CanonicalNavigable::has_remote_host() const
{
    if (is_top_level_traversable() || !m_active_document)
        return false;
    auto host = m_active_document->host();
    return host && host != reporting_page();
}

WebContentPage& CanonicalNavigable::remote_host() const
{
    VERIFY(has_remote_host());
    return *m_active_document->host();
}

void CanonicalNavigable::hand_pending_webdriver_commands_to(WebContentPage& new_host)
{
    auto& traversable = top_level_traversable();
    auto old_host = traversable.page_hosting(*this);
    if (!old_host)
        return;
    if (auto view = traversable.view(); view.has_value())
        view->move_pending_webdriver_commands_to_new_host({}, id(), *old_host, new_host);
}

// The active document is gone from the page hosting it, which represents the navigable remotely from now on, unless it
// hosts nothing of the tab any more, in which case it is discarded.
void CanonicalNavigable::detach_remote_host()
{
    if (!has_remote_host())
        return;
    NonnullRefPtr<WebContentPage> host = remote_host();
    m_active_document->set_unloaded();
    top_level_traversable().stop_hosting_in_page(*this, move(host));
}

RefPtr<WebContentPage> CanonicalNavigable::pending_host() const
{
    auto document = document_being_populated();
    if (!document)
        return nullptr;
    return document->host();
}

void CanonicalNavigable::discard_pending_host()
{
    if (auto document = document_being_populated())
        discard_document(*document);
}

// A document that never activated is discarded. The page chosen to host it drops the provisional navigable it created
// for it, unless it hosts the displayed document itself, and is released when it holds nothing else of the tab.
void CanonicalNavigable::discard_document(CanonicalDocument& document)
{
    auto page = document.host();
    if (!page)
        return;
    document.set_unloaded();
    if (is_hosted_by(*page))
        return;
    page->async_discard_provisional_navigable(id());
    top_level_traversable().release_page_if_unused(page.release_nonnull());
}

void CanonicalNavigable::set_viewport(Compositing::DevicePixelRect viewport_rect, Compositing::DevicePixelRect viewport_intersection, double device_pixel_ratio)
{
    m_viewport_rect = viewport_rect;
    m_viewport_intersection = viewport_intersection;
    m_device_pixel_ratio = device_pixel_ratio;
    send_viewport_to_host();
}

void CanonicalNavigable::send_viewport_to_host() const
{
    if (has_remote_host())
        send_viewport_to(remote_host());
    if (auto pending_host = this->pending_host(); pending_host && (!has_remote_host() || pending_host != &remote_host()))
        send_viewport_to(*pending_host);
}

void CanonicalNavigable::send_viewport_to(WebContentPage& host) const
{
    if (!m_viewport_rect.has_value())
        return;
    host.async_set_hosted_root_viewport(id(), m_viewport_rect->size(), m_viewport_intersection, m_device_pixel_ratio);
}

void CanonicalNavigable::set_replicated_state(Web::HTML::ReplicatedNavigableState state)
{
    m_active_session_history_entry_identity = state.active_session_history_entry_identity;
    m_document_blob_url = BlobURLHandle::for_url(blob_url_store(), state.active_document_url);
    m_replicated_state = move(state);
}

void CanonicalNavigable::update_container_state(Web::HTML::ReplicatedContainerState state)
{
    if (!m_replicated_state.has_value())
        return;
    m_replicated_state->container = state;
    if (has_remote_host())
        remote_host().async_update_local_root_container_state(id(), move(state));
}

void CanonicalNavigable::update_replicated_state(Web::HTML::ReplicatedNavigableState state)
{
    auto opener_changed = !m_replicated_state.has_value() || m_replicated_state->opener_navigable_id != state.opener_navigable_id;
    set_replicated_state(move(state));

    auto& traversable = top_level_traversable();
    Vector<NonnullRefPtr<WebContentClient>> clients;
    if (opener_changed) {
        traversable.for_each_hosting_page([&](WebContentPage& page) {
            if (!any_of(clients, [&](auto const& client) { return client.ptr() == &page.client(); }))
                clients.append(page.client());
        });
    }

    // Every process holding part of the tab holds the tab of a new opener before it hears of it.
    if (m_replicated_state->opener_navigable_id.has_value()) {
        for (auto& client : clients)
            traversable.represent_related_tabs_in(client);
    }

    traversable.for_each_page_representing(*this, [&](WebContentPage& page) {
        page.async_update_remote_navigable(id(), *m_replicated_state);
    });

    // A process can stop needing the tab of the previous opener.
    for (auto& client : clients)
        client->release_unneeded_representing_pages();
}

void CanonicalNavigable::active_document_completely_finished_loading()
{
    // The navigable's container runs the load event steps in the page hosting its parent's document, which is among
    // the pages representing the navigable.
    top_level_traversable().for_each_page_representing(*this, [&](WebContentPage& page) {
        page.async_content_navigable_completely_finished_loading(id());
    });
}

void CanonicalNavigable::set_current_session_history_entry(Web::HTML::SessionHistoryEntryDescriptor const& entry)
{
    m_current_session_history_entry_identity = Web::HTML::session_history_entry_identity(entry);
}

void CanonicalNavigable::set_active_session_history_entry(Web::HTML::SessionHistoryEntryDescriptor const& entry)
{
    m_active_session_history_entry_identity = Web::HTML::session_history_entry_identity(entry);
}

bool CanonicalNavigable::current_session_history_entry_is(Web::HTML::SessionHistoryEntryDescriptor const& entry) const
{
    return m_current_session_history_entry_identity.has_value()
        && *m_current_session_history_entry_identity == Web::HTML::session_history_entry_identity(entry);
}

bool CanonicalNavigable::active_document_is(Web::HTML::SessionHistoryEntryDescriptor const& entry) const
{
    return m_active_session_history_entry_identity.has_value()
        && m_active_session_history_entry_identity->document_state_id == entry.document_state.id;
}

void CanonicalNavigable::did_commit_navigation(Web::HTML::ReplicatedNavigableState replicated_state, Optional<Utf16String> const& navigation_id, DidPopulateDocument did_populate_document, RefPtr<CanonicalDocument> document)
{
    auto commits_ongoing_navigation = !m_ongoing_navigation.has_value()
        || !navigation_id.has_value()
        || navigation_id == m_ongoing_navigation->navigation_id;

    auto previous_active_document_state_id = m_active_session_history_entry_identity.has_value()
        ? Optional<Web::HTML::CrossProcessId> { m_active_session_history_entry_identity->document_state_id }
        : Optional<Web::HTML::CrossProcessId> {};
    auto active_document_changed = !previous_active_document_state_id.has_value()
        || replicated_state.active_session_history_entry_identity.document_state_id != *previous_active_document_state_id;

    if (!document && navigation_id.has_value() && commits_ongoing_navigation && m_ongoing_navigation.has_value())
        document = m_ongoing_navigation->document;
    if (!document && active_document_changed)
        document = document_created_by_host(replicated_state.active_document_origin);
    if (document) {
        // The page hosting the navigable created the document, when the UI process chose no page for it.
        if (!document->host()) {
            if (auto endpoint = top_level_traversable().page_hosting(*this))
                document->set_host(*endpoint);
        }
        set_active_document(*document);
        document->make_active();
    }
    update_replicated_state(move(replicated_state));

    // A navigation can commit while a newer navigation is already in flight. In that case update the replicated
    // state for the committed document without changing the newer navigation's transaction.
    if (!commits_ongoing_navigation)
        return;

    // The activated document's load becomes the navigable's tracked load. Reloads can reuse the document state,
    // while a same-document activation leaves the active document's load in place.
    if (active_document_changed || did_populate_document == DidPopulateDocument::Yes) {
        m_active_document_load = ActiveDocumentLoad {
            .navigation_id = m_ongoing_navigation.has_value() ? m_ongoing_navigation->navigation_id : Optional<Utf16String> {},
        };
    }

    clear_ongoing_navigation();
}

CanonicalNavigable::OngoingNavigation& CanonicalNavigable::ensure_ongoing_navigation()
{
    if (!m_ongoing_navigation.has_value())
        m_ongoing_navigation = OngoingNavigation {};
    return *m_ongoing_navigation;
}

void CanonicalNavigable::set_ongoing_navigation(OngoingNavigation ongoing_navigation)
{
    // NB: Taken before the handle covering this navigation's start is dropped below, so that a revoked entry is not
    //     let go of in between.
    auto blob_url = BlobURLHandle::for_url(blob_url_store(), ongoing_navigation.url);

    clear_superseded_navigation();
    m_navigation_blob_url = move(blob_url);
    m_ongoing_navigation = move(ongoing_navigation);
}

void CanonicalNavigable::set_ongoing_navigation_to_traversal(Web::HTML::CrossProcessId operation_id)
{
    m_ongoing_navigation_traversal_operation_id = operation_id;
}

void CanonicalNavigable::clear_ongoing_navigation_traversal(Web::HTML::CrossProcessId operation_id)
{
    if (m_ongoing_navigation_traversal_operation_id == operation_id)
        m_ongoing_navigation_traversal_operation_id.clear();
}

void CanonicalNavigable::clear_ongoing_navigation_state()
{
    m_ongoing_navigation.clear();
    m_ongoing_navigation_traversal_operation_id.clear();

    // NB: The navigation this covered has either been announced, and is held below, or is not coming.
    m_pending_navigation_blob_url = {};
    m_navigation_blob_url = {};
}

void CanonicalNavigable::clear_ongoing_navigation()
{
    discard_pending_host();
    clear_ongoing_navigation_state();
}

void CanonicalNavigable::clear_superseded_navigation()
{
    clear_ongoing_navigation();
}

BlobURLStore* CanonicalNavigable::blob_url_store() const
{
    auto page = reporting_page();
    return page ? page->client().session().blob_url_store.ptr() : nullptr;
}

void CanonicalNavigable::retain_blob_url_token(URL::BlobURLEntry::Token token)
{
    if (auto* store = blob_url_store())
        m_pending_navigation_blob_url = BlobURLHandle { *store, token };
}

void CanonicalNavigable::set_navigation_population_worker(WebContentPage& page)
{
    auto& ongoing_navigation = ensure_ongoing_navigation();
    VERIFY(!ongoing_navigation.population_worker);
    ongoing_navigation.population_worker = page;
}

bool CanonicalNavigable::navigation_population_matches(WebContentPage const& page, Utf16String const& navigation_id) const
{
    return m_ongoing_navigation.has_value()
        && m_ongoing_navigation->navigation_id == navigation_id
        && m_ongoing_navigation->phase == OngoingNavigation::Phase::Populating
        && navigation_population_worker_matches(page);
}

bool CanonicalNavigable::navigation_population_worker_matches(WebContentPage const& page) const
{
    return m_ongoing_navigation.has_value() && m_ongoing_navigation->population_worker.ptr() == &page;
}

void CanonicalNavigable::set_navigation_host(WebContentPage& page)
{
    auto& ongoing_navigation = ensure_ongoing_navigation();
    ongoing_navigation.host = page;
    // The page creates the document the navigation populates.
    if (ongoing_navigation.document)
        ongoing_navigation.document->set_host(page);

    // The population worker conducts the navigation until the hosting process takes over.
    ongoing_navigation.population_worker = nullptr;
}

bool CanonicalNavigable::navigation_host_matches(WebContentPage const& page) const
{
    return m_ongoing_navigation.has_value() && m_ongoing_navigation->host.ptr() == &page;
}

bool CanonicalNavigable::navigation_owner_matches(WebContentPage const& page) const
{
    return navigation_population_worker_matches(page) || navigation_host_matches(page);
}

bool CanonicalNavigable::navigation_transaction_matches(Utf16String const& navigation_id, WebContentPage const& page) const
{
    return m_ongoing_navigation.has_value()
        && m_ongoing_navigation->navigation_id == navigation_id
        && m_ongoing_navigation->phase == OngoingNavigation::Phase::Populating
        && navigation_host_matches(page);
}

bool CanonicalNavigable::cancel_navigation_transaction_for_client(WebContentClient& client)
{
    if (!m_ongoing_navigation.has_value())
        return false;

    auto is_page_of_client = [&](RefPtr<WebContentPage> const& page) {
        return page && &page->client() == &client;
    };
    if (!is_page_of_client(m_ongoing_navigation->population_worker) && !is_page_of_client(m_ongoing_navigation->host))
        return false;

    clear_ongoing_navigation();
    return true;
}

void CanonicalNavigable::did_finish_navigation_transaction(Optional<Utf16String> const& navigation_id, Web::HTML::HistoryStepResult result)
{
    if (!navigation_id.has_value())
        return;

    // A transaction still live at its operation's completion never activated its document.
    if (m_ongoing_navigation.has_value() && m_ongoing_navigation->navigation_id == navigation_id)
        clear_ongoing_navigation();

    if (result != Web::HTML::HistoryStepResult::Applied
        && m_active_document_load.navigation_id == navigation_id) {
        clear_active_document_load();
    }
}

bool CanonicalNavigable::matches_ongoing_navigation(Optional<Utf16String> const& navigation_id) const
{
    // A live transaction owns the view's loading state, so completion signals must name it.
    if (m_ongoing_navigation.has_value())
        return m_ongoing_navigation->has_started && navigation_id == m_ongoing_navigation->navigation_id;

    // Otherwise completion signals concern the active document's tracked load.
    return navigation_id == m_active_document_load.navigation_id;
}

}
