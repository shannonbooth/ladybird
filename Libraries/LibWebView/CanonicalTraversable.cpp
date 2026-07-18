/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ScopeGuard.h>
#include <LibCore/EventLoop.h>
#include <LibWebView/Application.h>
#include <LibWebView/CanonicalTraversable.h>
#include <LibWebView/HistoryDebug.h>
#include <LibWebView/SiteIsolationManager.h>
#include <LibWebView/WebContentClient.h>

namespace WebView {

static void add_participating_endpoint(CanonicalTraversable::TraversalOperation& operation, CanonicalTraversable::TraversalOperation::Endpoint endpoint);

CanonicalTraversable::CanonicalTraversable()
    : CanonicalNavigable({}, {}, nullptr, 0)
{
}

void CanonicalTraversable::complete_running_traversal_operation(Web::HTML::HistoryStepResult result, Optional<i32> committed_step)
{
    if (!m_running_traversal_operation.has_value())
        return;

    auto operation_id = m_running_traversal_operation->operation_id;
    auto participating_endpoints = move(m_running_traversal_operation->participating_endpoints);
    if (history_debug_enabled())
        dbgln("[History] operation complete id={} result={} committed_step={}", operation_id, (int)result, committed_step);
    m_running_traversal_operation.clear();
    for (auto const& endpoint : participating_endpoints) {
        if (!endpoint.client)
            continue;
        endpoint.client->async_complete_history_operation(endpoint.page_id, operation_id, result, committed_step, endpoint.initiation_id);
    }
    schedule_session_history_traversal_queue();
}

CanonicalNavigable& CanonicalTraversable::insert(WebContentClient& reporting_client, u64 page_id, Web::HTML::CrossProcessId parent_frame_id, Web::HTML::CrossProcessId frame_id, CanonicalNavigable& fallback_parent)
{
    if (auto existing_navigable = find(frame_id); existing_navigable.has_value())
        remove(*existing_navigable);

    auto navigable = make<CanonicalNavigable>(frame_id, parent_frame_id, &reporting_client, page_id);

    // A frame's parent frame is always created (and thus reported) before the frame
    // itself, so if the parent is not in the index, the parent is the top-level document
    // of the reporting page: the fallback parent.
    auto* parent = &fallback_parent;
    if (auto indexed_parent = find(parent_frame_id); indexed_parent.has_value())
        parent = &*indexed_parent;

    auto& navigable_ref = parent->append_child(move(navigable));
    m_navigable_index.set(navigable_ref.id(), navigable_ref.make_weak_ptr());
    return navigable_ref;
}

Optional<CanonicalNavigable&> CanonicalTraversable::find(Web::HTML::CrossProcessId navigable_id)
{
    if (id() == navigable_id)
        return *this;

    auto navigable = m_navigable_index.get(navigable_id);
    if (!navigable.has_value() || !navigable.value())
        return {};

    return *navigable.value();
}

Optional<CanonicalNavigable const&> CanonicalTraversable::find(Web::HTML::CrossProcessId navigable_id) const
{
    if (id() == navigable_id)
        return *this;

    auto navigable = m_navigable_index.get(navigable_id);
    if (!navigable.has_value() || !navigable.value())
        return {};

    return *navigable.value();
}

void CanonicalTraversable::remove(CanonicalNavigable& navigable)
{
    VERIFY(&navigable != this);
    remove_from_index(navigable);

    auto* parent = navigable.parent();
    VERIFY(parent);
    (void)parent->remove_child(navigable);
}

void CanonicalTraversable::remove_from_index(CanonicalNavigable& navigable)
{
    navigable.for_each_in_inclusive_subtree([&](CanonicalNavigable& child) {
        m_navigable_index.remove(child.id());
        return IterationDecision::Continue;
    });
}

static Optional<size_t> current_top_level_history_entry_index_for_step(Vector<Web::HTML::SessionHistoryEntryDescriptor> const& entries, Optional<i32> current_step)
{
    if (!current_step.has_value())
        return {};

    Optional<size_t> current_entry_index;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].document_state.navigable_target_name.is_empty())
            continue;

        if (entries[i].step <= *current_step)
            current_entry_index = i;
        if (entries[i].step >= *current_step)
            break;
    }
    return current_entry_index;
}

void CanonicalTraversable::abandon_pending_web_content_session_history_seed()
{
    m_session_history_entry_url_loading_from_ui_process.clear();
    m_pending_web_content_session_history_seed.clear();
}

void CanonicalTraversable::prepare_to_seed_web_content_session_history_from_ui_process()
{
    m_current_web_content_session_history_matches_mirror = false;
    m_session_history.forget_web_content_state();
    m_pending_session_history_navigation.clear();
    m_pending_web_content_session_history_seed.clear();
    m_pending_web_content_session_history_seed.step_after_loading_top_level_entry = m_session_history.current_step_to_restore_after_loading_top_level_entry();
    m_pending_web_content_session_history_seed.should_send_entries = true;
    m_pending_web_content_session_history_seed.ignore_updates_until_seed = true;
}

static bool can_seed_replacement_process_before_load(TraversableSessionHistory const& session_history, Optional<URL::URL> const& session_history_entry_url_loading_from_ui_process, PendingWebContentSessionHistorySeed const& pending_web_content_session_history_seed)
{
    if (!pending_web_content_session_history_seed.should_send_entries)
        return false;
    if (session_history_entry_url_loading_from_ui_process.has_value())
        return false;
    if (session_history.current_step_to_restore_after_loading_top_level_entry().has_value())
        return false;
    return true;
}

ProcessSwapNavigationPreparation CanonicalTraversable::prepare_for_process_swap_navigation(URL::URL const& url, Web::HTML::DocumentResource document_resource, Web::Bindings::NavigationHistoryBehavior history_handling)
{
    ProcessSwapNavigationPreparation result;

    auto ui_session_history_already_points_to_url = false;
    if (auto const* current_entry = m_session_history.current_entry(); current_entry && current_entry->url == url)
        ui_session_history_already_points_to_url = true;

    if (m_running_traversal_operation.has_value() && m_running_traversal_operation->will_replace_web_content_process)
        m_running_traversal_operation->stage = TraversalOperation::Stage::ReplacingWebContentProcess;
    if (m_pending_session_history_navigation.has_value())
        m_pending_session_history_navigation->web_content_restore_mode = PendingSessionHistoryNavigation::WebContentRestoreMode::RestoreFromUIProcess;
    m_current_web_content_session_history_matches_mirror = false;
    m_session_history.forget_web_content_state();
    m_pending_web_content_session_history_seed.waiting_for_ack = false;
    m_pending_web_content_session_history_seed.should_send_entries = true;
    m_pending_web_content_session_history_seed.ignore_updates_until_seed = true;

    if (!ui_session_history_already_points_to_url && !m_session_history_entry_url_loading_from_ui_process.has_value()) {
        if (m_session_history.current_entry()) {
            m_pending_session_history_navigation = PendingSessionHistoryNavigation {
                url,
                m_session_history,
                PendingSessionHistoryNavigation::WebContentRestoreMode::RestoreFromUIProcess,
            };
        } else {
            m_pending_session_history_navigation.clear();
        }

        // The in-flight target is display state only; the replacement process's commit creates the canonical entry.
        (void)document_resource;
        (void)history_handling;
        result.should_update_navigation_action_state = true;
    }

    if (!m_session_history_entry_url_loading_from_ui_process.has_value())
        m_pending_web_content_session_history_seed.step_after_loading_top_level_entry = m_session_history.current_step_to_restore_after_loading_top_level_entry();

    result.should_seed_web_content_before_load = can_seed_replacement_process_before_load(m_session_history, m_session_history_entry_url_loading_from_ui_process, m_pending_web_content_session_history_seed);
    return result;
}

PageLoadPreparation CanonicalTraversable::prepare_for_page_load(URL::URL const& url, Web::Bindings::NavigationHistoryBehavior history_handling)
{
    PageLoadPreparation result;

    if (m_session_history_entry_url_loading_from_ui_process.has_value())
        return result;

    abandon_pending_web_content_session_history_seed();
    complete_running_traversal_operation();
    auto const* current_entry = m_session_history.current_entry();
    auto is_javascript_navigation = url.scheme() == "javascript"sv;
    result.should_defer_ui_process_history_update = is_javascript_navigation;
    if (current_entry && !is_javascript_navigation)
        m_pending_session_history_navigation = PendingSessionHistoryNavigation { url, m_session_history };
    else
        m_pending_session_history_navigation.clear();

    if (is_javascript_navigation)
        return result;

    // The load target is display state only until the cross-document finalization commits the canonical entry.
    (void)history_handling;
    result.should_update_navigation_action_state = true;
    return result;
}

void CanonicalTraversable::prepare_for_non_history_page_load()
{
    abandon_pending_web_content_session_history_seed();
    m_current_web_content_session_history_matches_mirror = false;
    m_session_history.forget_web_content_state();
}

void CanonicalTraversable::prepare_for_reload()
{
    abandon_pending_web_content_session_history_seed();
    m_session_history.mark_current_entry_reload_pending();
    m_current_web_content_session_history_matches_mirror = false;
}

bool CanonicalTraversable::did_create_top_level_traversable(Web::HTML::SessionHistoryEntryDescriptor initial_history_entry)
{
    if (!m_session_history.initialize_with_initial_history_entry(move(initial_history_entry)))
        return false;

    m_current_web_content_session_history_matches_mirror = true;
    return true;
}

bool CanonicalTraversable::update_session_history_entry_navigation_api_state(CanonicalNavigable const& navigable, Utf16String const& navigation_api_key, Web::HTML::StorageSerializationRecord navigation_api_state)
{
    VERIFY(&navigable.top_level_traversable() == this);

    if (m_pending_web_content_session_history_seed.ignore_updates_until_seed)
        return false;

    if (&navigable == this)
        return m_session_history.update_top_level_navigation_api_state(navigation_api_key, move(navigation_api_state));
    return m_session_history.update_nested_navigation_api_state(navigable.id(), navigation_api_key, move(navigation_api_state));
}

bool CanonicalTraversable::update_session_history_entry_scroll_restoration_mode(CanonicalNavigable const& navigable, Utf16String const& navigation_api_key, Web::HTML::ScrollRestorationMode scroll_restoration_mode)
{
    VERIFY(&navigable.top_level_traversable() == this);

    if (m_pending_web_content_session_history_seed.ignore_updates_until_seed)
        return false;

    if (&navigable == this)
        return m_session_history.update_top_level_scroll_restoration_mode(navigation_api_key, scroll_restoration_mode);
    return m_session_history.update_nested_scroll_restoration_mode(navigable.id(), navigation_api_key, scroll_restoration_mode);
}

bool CanonicalTraversable::update_session_history_entry_scroll_position_data(CanonicalNavigable const& navigable, Utf16String const& navigation_api_key, Web::HTML::SessionHistoryEntryScrollPositionData scroll_position_data)
{
    VERIFY(&navigable.top_level_traversable() == this);

    if (m_pending_web_content_session_history_seed.ignore_updates_until_seed)
        return false;

    if (&navigable == this)
        return m_session_history.update_top_level_scroll_position_data(navigation_api_key, move(scroll_position_data));
    return m_session_history.update_nested_scroll_position_data(navigable.id(), navigation_api_key, move(scroll_position_data));
}

bool CanonicalTraversable::update_session_history_entry_document_state_navigable_target_name(CanonicalNavigable const& navigable, Utf16String const& navigation_api_key, Utf16String navigable_target_name)
{
    VERIFY(&navigable.top_level_traversable() == this);

    if (m_pending_web_content_session_history_seed.ignore_updates_until_seed)
        return false;

    if (&navigable == this)
        return m_session_history.update_top_level_document_state_navigable_target_name(navigation_api_key, move(navigable_target_name));
    return m_session_history.update_nested_document_state_navigable_target_name(navigable.id(), navigation_api_key, move(navigable_target_name));
}

bool CanonicalTraversable::set_session_history_entry_document_state_reload_pending(CanonicalNavigable const& navigable, Utf16String const& navigation_api_key, bool reload_pending)
{
    VERIFY(&navigable.top_level_traversable() == this);

    if (m_pending_web_content_session_history_seed.ignore_updates_until_seed)
        return false;

    auto did_update = &navigable == this
        ? m_session_history.set_top_level_document_state_reload_pending(navigation_api_key, reload_pending)
        : m_session_history.set_nested_document_state_reload_pending(navigable.id(), navigation_api_key, reload_pending);
    if (did_update)
        m_current_web_content_session_history_matches_mirror = m_session_history.web_content_history_matches_mirror();
    return did_update;
}

bool CanonicalTraversable::append_nested_history(CanonicalNavigable const& parent_navigable, Web::HTML::SessionHistoryNestedHistoryDescriptor nested_history)
{
    VERIFY(&parent_navigable.top_level_traversable() == this);

    if (m_pending_web_content_session_history_seed.ignore_updates_until_seed)
        return false;

    auto child_navigable = find(nested_history.id);
    if (!child_navigable.has_value() || child_navigable->parent() != &parent_navigable)
        return false;
    return m_session_history.append_nested_history(parent_navigable, move(nested_history));
}

bool CanonicalTraversable::remove_nested_history(CanonicalNavigable const& parent_navigable, Web::HTML::CrossProcessId child_navigable_id)
{
    VERIFY(&parent_navigable.top_level_traversable() == this);

    if (m_pending_web_content_session_history_seed.ignore_updates_until_seed)
        return false;

    return m_session_history.remove_nested_history(parent_navigable, child_navigable_id);
}

void CanonicalTraversable::request_to_finalize_same_document_navigation(CanonicalNavigable const& navigable, Utf16String expected_current_navigation_api_key, Web::HTML::SameDocumentNavigationEntry target_entry, Optional<Utf16String> entry_to_replace_navigation_api_key, Function<void(Optional<SameDocumentNavigationCommitResult>)> on_complete)
{
    VERIFY(&navigable.top_level_traversable() == this);

    if (m_pending_web_content_session_history_seed.ignore_updates_until_seed) {
        on_complete({});
        return;
    }

    // https://html.spec.whatwg.org/multipage/document-sequences.html#tn-session-history-traversal-queue
    // Append session history synchronous navigation steps involving navigable to traversable.
    m_session_history_traversal_queue.append(SynchronousNavigationSteps {
        .target_navigable_id = navigable.id(),
        .expected_current_navigation_api_key = move(expected_current_navigation_api_key),
        .target_entry = move(target_entry),
        .entry_to_replace_navigation_api_key = move(entry_to_replace_navigation_api_key),
        .on_complete = move(on_complete),
    });

    // WebContent has synchronously installed the provisional entry. Use the canonical must-wait set to insert this item
    // at the specification's queue-jumping point before replying. Once apply the history step is coordinated here, the
    // same queue item will be consumed directly by that algorithm.
    if (!m_running_traversal_operation.has_value()) {
        run_session_history_traversal_queue();
    } else if (m_running_traversal_operation->stage == TraversalOperation::Stage::ApplyingHistoryStep
        && m_running_traversal_operation->apply_history_step.has_value()) {
        while (run_first_queued_synchronous_navigation_steps_not_targeting(
            m_running_traversal_operation->apply_history_step->navigables_that_must_wait_before_handling_sync_navigation)) {
        }
    }
}

void CanonicalTraversable::run_synchronous_navigation_steps(SynchronousNavigationSteps steps)
{
    Optional<SameDocumentNavigationCommitResult> result;
    if (auto navigable = find(steps.target_navigable_id); navigable.has_value()) {
        // https://html.spec.whatwg.org/multipage/browsing-the-web.html#finalize-a-same-document-navigation
        result = m_session_history.finalize_same_document_navigation(
            *navigable,
            steps.expected_current_navigation_api_key,
            move(steps.target_entry),
            move(steps.entry_to_replace_navigation_api_key));
    }
    steps.on_complete(move(result));
}

bool CanonicalTraversable::run_first_queued_synchronous_navigation_steps_not_targeting(HashTable<Web::HTML::CrossProcessId> const& excluded_navigables)
{
    auto index = m_session_history_traversal_queue.find_first_index_if([&](auto const& entry) {
        return entry.template has<SynchronousNavigationSteps>()
            && !excluded_navigables.contains(entry.template get<SynchronousNavigationSteps>().target_navigable_id);
    });
    if (!index.has_value())
        return false;

    auto entry = m_session_history_traversal_queue.take(*index);
    run_synchronous_navigation_steps(move(entry.get<SynchronousNavigationSteps>()));
    return true;
}

void CanonicalTraversable::run_session_history_traversal_queue()
{
    if (m_running_traversal_operation.has_value() || m_is_running_session_history_traversal_queue || m_session_history_traversal_queue_run_scheduled)
        return;

    m_is_running_session_history_traversal_queue = true;
    ScopeGuard reset_running_queue_state = [this] {
        m_is_running_session_history_traversal_queue = false;
    };

    while (!m_session_history_traversal_queue.is_empty()) {
        auto entry = m_session_history_traversal_queue.take_first();
        if (entry.has<SynchronousNavigationSteps>()) {
            run_synchronous_navigation_steps(move(entry.get<SynchronousNavigationSteps>()));
            continue;
        }

        m_running_traversal_operation.emplace(move(entry.get<TraversalOperation>()));
        start_running_traversal_operation();
        if (m_running_traversal_operation.has_value())
            return;
    }
}

void CanonicalTraversable::schedule_session_history_traversal_queue()
{
    if (m_session_history_traversal_queue_run_scheduled)
        return;

    m_session_history_traversal_queue_run_scheduled = true;
    auto weak_this = make_weak_ptr();
    Core::deferred_invoke([weak_this] {
        auto* navigable = weak_this.ptr();
        if (!navigable)
            return;
        auto& traversable = static_cast<CanonicalTraversable&>(*navigable);
        traversable.m_session_history_traversal_queue_run_scheduled = false;
        traversable.run_session_history_traversal_queue();
    });
}

void CanonicalTraversable::request_to_finalize_cross_document_navigation(WebContentClient& client, u64 page_id, u64 initiation_id, CanonicalNavigable const& navigable, Web::HTML::SessionHistoryEntryDescriptor history_entry, Optional<Utf16String> entry_to_replace_navigation_api_key, Function<void()> on_committed)
{
    VERIFY(&navigable.top_level_traversable() == this);

    TraversalOperation::Endpoint endpoint {
        .client = client,
        .page_id = page_id,
        .initiation_id = initiation_id,
    };
    append_session_history_traversal_operation({
        .requested_traversal = TraversalOperation::FinalizeCrossDocument {
            .navigable_id = navigable.id(),
            .history_entry = move(history_entry),
            .entry_to_replace_navigation_api_key = move(entry_to_replace_navigation_api_key),
            .on_committed = move(on_committed),
        },
        .check_for_cancelation = CheckForCancelation::No,
        .user_involvement = Web::HTML::UserNavigationInvolvement::None,
        .initiating_endpoint = endpoint,
        .root_endpoint = endpoint,
    });
}

void CanonicalTraversable::start_running_finalize_cross_document_operation()
{
    VERIFY(m_running_traversal_operation.has_value());
    auto& operation = *m_running_traversal_operation;
    auto& request = operation.requested_traversal.get<TraversalOperation::FinalizeCrossDocument>();

    // The terminal completion must reach the initiating process even when no job is dispatched, so its parked
    // finalization state is always released.
    if (operation.initiating_endpoint.has_value())
        add_participating_endpoint(operation, *operation.initiating_endpoint);

    auto complete_without_canonical_commit = [&] {
        // AD-HOC: A commit the canonical history cannot apply (removed frame, unknown replaced entry, pending seed)
        //         leaves WebContent's already-committed local view authoritative for that document. The mirror
        //         mismatch is handled by the existing reseed fallback.
        m_current_web_content_session_history_matches_mirror = false;
        complete_running_traversal_operation(Web::HTML::HistoryStepResult::Applied);
    };

    if (m_pending_web_content_session_history_seed.ignore_updates_until_seed) {
        complete_without_canonical_commit();
        return;
    }

    auto navigable = find(request.navigable_id);
    if (!navigable.has_value()) {
        dbgln("Dropping cross-document finalization for unknown navigable");
        complete_without_canonical_commit();
        return;
    }

    auto commit = m_session_history.finalize_cross_document_navigation(*navigable, request.history_entry, request.entry_to_replace_navigation_api_key);
    if (!commit.has_value()) {
        dbgln("Dropping stale cross-document finalization for navigable");
        complete_without_canonical_commit();
        return;
    }
    m_current_web_content_session_history_matches_mirror = m_session_history.web_content_history_matches_mirror();
    if (request.on_committed)
        request.on_committed();

    // 10. Apply the push/replace history step targetStep to traversable given historyHandling and userInvolvement.
    // This runs apply the history step with checkForCancelation false. The document was already populated by the
    // navigation, so the changing job consumes the initiating process's parked pending document.
    operation.target_step = commit->target_step;
    operation.target_step_index = commit->target_step_index;
    operation.can_apply_in_current_web_content = true;
    operation.stage = TraversalOperation::Stage::ApplyingHistoryStep;
    operation.apply_history_step.emplace();
    dispatch_changing_navigable_history_jobs();
}

Optional<i32> CanonicalTraversable::navigation_api_traversal_target(CanonicalNavigable const& navigable, Utf16String const& navigation_api_key) const
{
    VERIFY(&navigable.top_level_traversable() == this);

    // 1. Let navigableSHEs be the result of getting session history entries given navigable.
    auto navigable_session_history_entries = m_session_history.get_session_history_entries(navigable);
    if (!navigable_session_history_entries.has_value())
        return {};

    // 2. Let targetSHE be the session history entry in navigableSHEs whose navigation API key is key. If no such entry exists, then:
    auto target_entry = navigable_session_history_entries->find_if([&](auto const& entry) {
        return entry.navigation_api_key == navigation_api_key;
    });
    if (target_entry == navigable_session_history_entries->end())
        return {};

    return target_entry->step;
}

WebContentSessionHistoryUpdateResult CanonicalTraversable::update_session_history_from_web_content(Vector<Web::HTML::SessionHistoryEntryDescriptor> entries, Vector<i32> used_steps, size_t current_used_step_index, bool pending_step_after_fallback_load_was_restored, bool seed_web_content_on_invalid_snapshot, URL::URL const& current_url)
{
    auto update_result = m_session_history.update_from_web_content(move(entries), move(used_steps), current_used_step_index);
    m_current_web_content_session_history_matches_mirror = update_result == TraversableSessionHistory::UpdateResult::CompleteSnapshot
        && m_session_history.web_content_history_matches_mirror();

    WebContentSessionHistoryUpdateResult result {
        .update_result = update_result,
    };

    if (update_result != TraversableSessionHistory::UpdateResult::InvalidSnapshot) {
        if (update_result == TraversableSessionHistory::UpdateResult::CompleteSnapshot)
            m_pending_session_history_navigation.clear();
        if (auto* current_entry = m_session_history.current_entry())
            result.current_url = current_entry->url;
        if (pending_step_after_fallback_load_was_restored)
            m_pending_web_content_session_history_seed.step_after_loading_top_level_entry.clear();
    } else if (seed_web_content_on_invalid_snapshot) {
        if (auto const* current_entry = m_session_history.current_entry(); current_entry && current_entry->url == current_url) {
            prepare_to_seed_web_content_session_history_from_ui_process();
            result.should_seed_web_content = true;
        }
    }

    return result;
}

WebContentSessionHistoryUpdateResult CanonicalTraversable::adopt_web_content_session_history_after_rejected_seed(Vector<Web::HTML::SessionHistoryEntryDescriptor> entries, Vector<i32> used_steps, size_t current_used_step_index, URL::URL const& current_url)
{
    if (entries.is_empty())
        return {};

    auto entries_from_web_content = entries;
    auto used_steps_from_web_content = used_steps;
    auto update = update_session_history_from_web_content(move(entries), move(used_steps), current_used_step_index, false, false, current_url);
    if (update.update_result == TraversableSessionHistory::UpdateResult::InvalidSnapshot && current_used_step_index < used_steps_from_web_content.size()) {
        auto current_top_level_entry_index = current_top_level_history_entry_index_for_step(entries_from_web_content, used_steps_from_web_content[current_used_step_index]);
        if (current_top_level_entry_index.has_value() && entries_from_web_content[*current_top_level_entry_index].url == current_url) {
            m_session_history.clear();
            update = update_session_history_from_web_content(move(entries_from_web_content), move(used_steps_from_web_content), current_used_step_index, false, false, current_url);
        }
    }
    if (update.update_result == TraversableSessionHistory::UpdateResult::InvalidSnapshot)
        return update;

    m_pending_web_content_session_history_seed.clear();
    complete_running_traversal_operation();
    return update;
}

WebContentSessionHistorySeedAckResult CanonicalTraversable::did_receive_web_content_session_history_seed_ack(bool accepted, Vector<Web::HTML::SessionHistoryEntryDescriptor> entries, Vector<i32> used_steps, size_t current_used_step_index, URL::URL const& current_url)
{
    if (!m_pending_web_content_session_history_seed.waiting_for_ack)
        return { .ignored = true, .dump_reason = "ignored-webcontent-session-history-seed-ack"sv };

    WebContentSessionHistorySeedAckResult result;
    result.should_update_navigation_action_state = true;

    if (!accepted) {
        auto update = adopt_web_content_session_history_after_rejected_seed(move(entries), move(used_steps), current_used_step_index, current_url);
        if (update.update_result != TraversableSessionHistory::UpdateResult::InvalidSnapshot) {
            result.dump_reason = "webcontent-session-history-seed-rejected-with-current-snapshot"sv;
            result.current_url = move(update.current_url);
            // NB: Applying the adopted snapshot's current URL already refreshes the navigation actions.
            result.should_update_navigation_action_state = false;
            return result;
        }

        abandon_pending_web_content_session_history_seed();
        m_current_web_content_session_history_matches_mirror = false;
        m_session_history.forget_web_content_state();
        complete_running_traversal_operation();
        result.dump_reason = "webcontent-session-history-seed-rejected"sv;
        return result;
    }

    if (!m_session_history.did_seed_web_content_from_ui_process(move(entries), move(used_steps), current_used_step_index)) {
        if (m_pending_web_content_session_history_seed.should_reseed_after_current_history_load) {
            m_pending_web_content_session_history_seed.waiting_for_ack = false;
            m_pending_web_content_session_history_seed.should_send_entries = true;
            m_pending_web_content_session_history_seed.ignore_updates_until_seed = true;
            m_current_web_content_session_history_matches_mirror = false;
            result.dump_reason = "webcontent-session-history-preload-seed-ack-mismatch"sv;
            return result;
        }

        abandon_pending_web_content_session_history_seed();
        m_current_web_content_session_history_matches_mirror = false;
        m_session_history.forget_web_content_state();
        complete_running_traversal_operation();
        result.dump_reason = "webcontent-session-history-seed-ack-mismatch"sv;
        return result;
    }

    reconcile_child_navigable_ids_after_session_history_reconstruction();

    m_pending_web_content_session_history_seed.waiting_for_ack = false;
    if (m_pending_web_content_session_history_seed.should_reseed_after_current_history_load) {
        m_pending_web_content_session_history_seed.should_send_entries = true;
        m_pending_web_content_session_history_seed.ignore_updates_until_seed = true;
        m_current_web_content_session_history_matches_mirror = false;
        result.dump_reason = "webcontent-session-history-preload-seed-ack"sv;
        return result;
    }

    m_pending_web_content_session_history_seed.ignore_updates_until_seed = false;
    m_current_web_content_session_history_matches_mirror = !m_pending_web_content_session_history_seed.step_after_loading_top_level_entry.has_value()
        && !m_pending_session_history_navigation.has_value();
    if (m_pending_web_content_session_history_seed.step_after_loading_top_level_entry.has_value()) {
        result.step_to_traverse = *m_pending_web_content_session_history_seed.step_after_loading_top_level_entry;
    } else {
        auto is_waiting_for_history_step_cancelation_check = m_running_traversal_operation.has_value()
            && m_running_traversal_operation->stage == TraversalOperation::Stage::CheckingCancelation;
        if (!is_waiting_for_history_step_cancelation_check) {
            complete_running_traversal_operation();
            result.should_complete_webdriver_pending_navigation = !m_pending_session_history_navigation.has_value();
        }
    }

    result.dump_reason = "webcontent-session-history-seed-ack"sv;
    return result;
}

void CanonicalTraversable::reconcile_child_navigable_ids_after_session_history_reconstruction()
{
    auto const* current_entry = m_session_history.current_entry();
    if (!current_entry)
        return;

    auto const& nested_histories = current_entry->document_state.nested_histories;
    if (children().size() != nested_histories.size())
        return;

    // FIXME: This mirrors WebContent's temporary glue for the current load-then-seed ordering. A replacement process
    //        can create live child navigables before receiving the canonical session-history tree. WebContent retargets
    //        those children to the canonical nested-history ids when applying the seed; keep the UI-owned navigable
    //        tree in the same id space so subsequent apply-the-history-step jobs can find them.
    for (size_t i = 0; i < nested_histories.size(); ++i) {
        auto& child_navigable = *children()[i];
        auto canonical_id = nested_histories[i].id;
        if (child_navigable.id() == canonical_id)
            continue;

        if (auto existing_navigable = find(canonical_id); existing_navigable.has_value() && &*existing_navigable != &child_navigable)
            continue;

        m_navigable_index.remove(child_navigable.id());
        child_navigable.set_id(canonical_id);
        m_navigable_index.set(canonical_id, child_navigable.make_weak_ptr());
    }
}

NavigationStartResult CanonicalTraversable::did_start_navigation(URL::URL const& url, Web::HTML::DocumentResource document_resource, bool is_redirect, Web::Bindings::NavigationHistoryBehavior history_handling, bool is_showing_crash_page)
{
    if (m_session_history_entry_url_loading_from_ui_process.has_value()) {
        if (*m_session_history_entry_url_loading_from_ui_process != url)
            return { .dump_reason = "ignored-stale-ui-history-load-start"sv };

        auto should_keep_preseeded_web_content_history = m_pending_web_content_session_history_seed.waiting_for_ack || m_session_history.web_content_uses_ui_step_coordinates();
        m_session_history_entry_url_loading_from_ui_process.clear();
        if (!should_keep_preseeded_web_content_history) {
            m_current_web_content_session_history_matches_mirror = false;
            m_session_history.forget_web_content_state();
        }
        return { .dump_reason = "did-start-navigation-from-ui-history-load"sv };
    }

    if (m_pending_web_content_session_history_seed.should_send_entries || m_pending_web_content_session_history_seed.ignore_updates_until_seed || m_pending_web_content_session_history_seed.waiting_for_ack) {
        if (auto const* current_entry = m_session_history.current_entry(); current_entry && current_entry->url != url)
            return { .dump_reason = "ignored-navigation-start-before-ui-history-seed"sv };
    }

    if (is_showing_crash_page) {
        if (auto const* current_entry = m_session_history.current_entry(); current_entry && current_entry->url == url) {
            prepare_to_seed_web_content_session_history_from_ui_process();
            return { .dump_reason = "did-start-navigation-from-crash-page"sv, .did_clear_crash_page = true };
        }
    }

    // AD-HOC: A navigation that has started loading is display state, not a canonical session history entry. The
    //         canonical entry list contains only committed and explicitly reported entries; the commit arrives
    //         through the finalize operation on the traversal queue.
    (void)document_resource;

    if (is_redirect) {
        if (m_pending_session_history_navigation.has_value())
            m_pending_session_history_navigation->url = url;
        return { .dump_reason = "did-start-navigation-redirect"sv, .should_update_navigation_action_state = true, .should_update_webdriver_pending_navigation_url = true, .did_clear_crash_page = is_showing_crash_page };
    }

    if (auto const* current_entry = m_session_history.current_entry(); current_entry && current_entry->url == url) {
        if (m_pending_session_history_navigation.has_value() && m_pending_session_history_navigation->url == url)
            return { .did_clear_crash_page = is_showing_crash_page };

        if (history_handling == Web::Bindings::NavigationHistoryBehavior::Push && m_current_web_content_session_history_matches_mirror)
            m_pending_session_history_navigation = PendingSessionHistoryNavigation { url, m_session_history };
        else
            m_pending_session_history_navigation.clear();

        if (history_handling == Web::Bindings::NavigationHistoryBehavior::Replace)
            return { .dump_reason = "did-start-navigation-replace-current-url"sv, .should_update_navigation_action_state = true, .did_clear_crash_page = is_showing_crash_page };
        if (history_handling == Web::Bindings::NavigationHistoryBehavior::Push)
            return { .dump_reason = "did-start-navigation-push-current-url"sv, .should_update_navigation_action_state = true, .did_clear_crash_page = is_showing_crash_page };
        return { .did_clear_crash_page = is_showing_crash_page };
    }

    if (m_session_history.current_entry())
        m_pending_session_history_navigation = PendingSessionHistoryNavigation { url, m_session_history };
    else
        m_pending_session_history_navigation.clear();
    return { .dump_reason = "did-start-navigation"sv, .should_update_navigation_action_state = true, .did_clear_crash_page = is_showing_crash_page };
}

NavigationCancelResult CanonicalTraversable::did_cancel_navigation(URL::URL const& url, bool has_webdriver_pending_navigation)
{
    if (m_pending_session_history_navigation.has_value() && m_pending_session_history_navigation->url == url)
        return { .status = NavigationCancelStatus::RestorePendingSessionHistoryNavigation };

    if (m_session_history_entry_url_loading_from_ui_process.has_value() && *m_session_history_entry_url_loading_from_ui_process == url) {
        m_session_history_entry_url_loading_from_ui_process.clear();
        abandon_pending_web_content_session_history_seed();
        m_current_web_content_session_history_matches_mirror = false;
        m_session_history.forget_web_content_state();
        return { .status = NavigationCancelStatus::CanceledUIHistoryLoad };
    }

    if (has_webdriver_pending_navigation) {
        m_session_history.clear_current_entry_reload_pending();
        return { .status = NavigationCancelStatus::CompleteWebdriverPendingNavigation };
    }

    return {};
}

NavigationFinishResult CanonicalTraversable::did_finish_navigation(URL::URL const& url)
{
    NavigationFinishResult result;
    if (m_pending_session_history_navigation.has_value()) {
        if (m_pending_session_history_navigation->url == url) {
            m_pending_session_history_navigation.clear();
        } else if (auto const* current_entry = m_session_history.current_entry(); current_entry && current_entry->url == url) {
            // A redirect can make the committed URL differ from the provisional URL stored when navigation began.
            // The matching load-finish notification confirms the final URL for the pending navigation.
            m_pending_session_history_navigation.clear();
            result.should_update_webdriver_pending_navigation_url = true;
        }
    }

    if (!m_pending_web_content_session_history_seed.should_send_entries)
        return result;

    if (auto const* current_entry = m_session_history.current_entry(); current_entry && current_entry->url == url) {
        m_session_history.clear_current_entry_reload_pending();
        auto allow_current_entry_reconstruction = m_pending_web_content_session_history_seed.should_reseed_after_current_history_load;
        m_pending_web_content_session_history_seed.should_reseed_after_current_history_load = false;
        result.should_seed_web_content = true;
        result.allow_current_entry_reconstruction = allow_current_entry_reconstruction;
        return result;
    }

    // NB: The first finish notification from a fresh WebContent process can still report about:blank before the
    //     traversed-to entry is ready. Keep the pending seed state intact so partial snapshots remain ignored
    //     until we can seed the full UI-owned history.
    result.dump_reason = "skip-seed-webcontent-session-history"sv;
    return result;
}

RestorePendingSessionHistoryNavigationResult CanonicalTraversable::restore_pending_session_history_navigation()
{
    if (!m_pending_session_history_navigation.has_value())
        return {};

    auto web_content_restore_mode = m_pending_session_history_navigation->web_content_restore_mode;
    m_session_history = move(m_pending_session_history_navigation->previous_session_history);
    m_pending_session_history_navigation.clear();
    complete_running_traversal_operation();

    RestorePendingSessionHistoryNavigationResult result { .restored = true, .web_content_restore_mode = web_content_restore_mode };
    if (auto* current_entry = m_session_history.current_entry()) {
        result.current_url = current_entry->url;
        if (web_content_restore_mode == PendingSessionHistoryNavigation::WebContentRestoreMode::PreserveCurrentProcessState) {
            m_session_history_entry_url_loading_from_ui_process.clear();
            abandon_pending_web_content_session_history_seed();
            m_current_web_content_session_history_matches_mirror = m_session_history.web_content_history_matches_mirror();
        }
    } else {
        m_current_web_content_session_history_matches_mirror = false;
    }
    return result;
}

void CanonicalTraversable::append_reload_history_step(Function<void()> steps)
{
    append_session_history_traversal_operation({
        .requested_traversal = TraversalOperation::Reload { move(steps) },
        .check_for_cancelation = CheckForCancelation::No,
    });
}

void CanonicalTraversable::traverse_the_history_by_delta(int delta, CheckForCancelation check_for_cancelation, URL::URL const& current_url, Function<void(HistoryTraversalOutcome)> on_cancelation_check_complete, Function<void(HistoryTraversalDecision)> on_ready_to_start, Optional<TraversalOperation::Endpoint> initiating_endpoint, Web::HTML::UserNavigationInvolvement user_involvement)
{
    append_session_history_traversal_operation({
        .requested_traversal = TraversalOperation::ByDelta { delta },
        .check_for_cancelation = check_for_cancelation,
        .user_involvement = user_involvement,
        .current_url = current_url,
        .on_ready_to_start = move(on_ready_to_start),
        .initiating_endpoint = move(initiating_endpoint),
        .on_cancelation_check_complete = move(on_cancelation_check_complete),
    });
}

void CanonicalTraversable::traverse_the_history_by_navigation_api_key(Web::HTML::CrossProcessId navigable_id, Utf16String key, URL::URL const& current_url, Function<void(HistoryTraversalDecision)> on_ready_to_start, TraversalOperation::Endpoint initiating_endpoint)
{
    append_session_history_traversal_operation({
        .requested_traversal = TraversalOperation::ByNavigationAPIKey { navigable_id, move(key) },
        .check_for_cancelation = CheckForCancelation::Yes,
        .user_involvement = Web::HTML::UserNavigationInvolvement::None,
        .current_url = current_url,
        .on_ready_to_start = move(on_ready_to_start),
        .initiating_endpoint = move(initiating_endpoint),
    });
}

void CanonicalTraversable::traverse_the_history_to_step(i32 step, CheckForCancelation check_for_cancelation, URL::URL const& current_url, Function<void(HistoryTraversalOutcome)> on_cancelation_check_complete, Function<void(HistoryTraversalDecision)> on_ready_to_start, Optional<TraversalOperation::Endpoint> initiating_endpoint, Web::HTML::UserNavigationInvolvement user_involvement)
{
    append_session_history_traversal_operation({
        .requested_traversal = TraversalOperation::ToStep { step },
        .check_for_cancelation = check_for_cancelation,
        .user_involvement = user_involvement,
        .current_url = current_url,
        .on_ready_to_start = move(on_ready_to_start),
        .initiating_endpoint = move(initiating_endpoint),
        .on_cancelation_check_complete = move(on_cancelation_check_complete),
    });
}

void CanonicalTraversable::append_session_history_traversal_operation(TraversalOperation operation)
{
    // https://html.spec.whatwg.org/multipage/document-sequences.html#tn-session-history-traversal-queue
    operation.operation_id = m_next_history_operation_id++;
    if (operation.initiating_endpoint.has_value()) {
        operation.root_endpoint = operation.initiating_endpoint;
        operation.participating_endpoints.append(*operation.initiating_endpoint);
    }
    m_session_history_traversal_queue.append(move(operation));
    run_session_history_traversal_queue();
}

void CanonicalTraversable::start_running_traversal_operation()
{
    VERIFY(m_running_traversal_operation.has_value());
    auto& operation = *m_running_traversal_operation;
    if (history_debug_enabled())
        dbgln("[History] operation start id={} kind={} queued={}", operation.operation_id, operation.requested_traversal.index(), m_session_history_traversal_queue.size());

    if (operation.requested_traversal.has<TraversalOperation::ResetForTesting>()) {
        auto operation_id = operation.operation_id;
        auto start = move(operation.requested_traversal.get<TraversalOperation::ResetForTesting>().start);
        VERIFY(start);
        start(operation_id);
        return;
    }

    if (operation.requested_traversal.has<TraversalOperation::FinalizeCrossDocument>()) {
        start_running_finalize_cross_document_operation();
        return;
    }

    // FIXME: Reload still enters the canonical traversal queue but its document-population path has not yet been
    // routed through the per-navigable job protocol. Convert it after cross-document finalize joins this queue.
    if (operation.requested_traversal.has<TraversalOperation::Reload>()) {
        auto steps = move(operation.requested_traversal.get<TraversalOperation::Reload>().steps);
        m_running_traversal_operation.clear();
        if (steps)
            steps();
        schedule_session_history_traversal_queue();
        return;
    }

    Optional<TraversableSessionHistory::TraversalTarget> target;
    if (operation.requested_traversal.has<TraversalOperation::ByDelta>())
        target = m_session_history.traversal_target_for_delta(operation.requested_traversal.get<TraversalOperation::ByDelta>().delta);
    else if (operation.requested_traversal.has<TraversalOperation::ToStep>())
        target = m_session_history.traversal_target_for_step(operation.requested_traversal.get<TraversalOperation::ToStep>().step);
    else {
        auto const& request = operation.requested_traversal.get<TraversalOperation::ByNavigationAPIKey>();
        if (auto navigable = find(request.navigable_id); navigable.has_value()) {
            if (auto step = navigation_api_traversal_target(*navigable, request.key); step.has_value())
                target = m_session_history.traversal_target_for_step(*step);
        }
    }

    if (!target.has_value()) {
        auto is_navigation_api_request = operation.requested_traversal.has<TraversalOperation::ByNavigationAPIKey>();
        auto on_outcome = move(operation.on_cancelation_check_complete);
        if (operation.on_ready_to_start)
            operation.on_ready_to_start({ .outcome = { .status = HistoryTraversalStatus::NoEntry } });
        if (on_outcome)
            on_outcome({ .status = HistoryTraversalStatus::NoEntry });
        complete_running_traversal_operation(is_navigation_api_request
                ? Web::HTML::HistoryStepResult::CanceledByMissingPage
                : Web::HTML::HistoryStepResult::Applied,
            { });
        return;
    }

    auto decision = traverse_the_history(*target);
    auto outcome = decision.outcome;
    if (operation.on_ready_to_start)
        operation.on_ready_to_start(move(decision));
    if (!outcome.waiting_for_cancelation_check && m_running_traversal_operation.has_value()) {
        auto on_outcome = move(m_running_traversal_operation->on_cancelation_check_complete);
        if (on_outcome)
            on_outcome(move(outcome));
    }
}

HistoryTraversalDecision CanonicalTraversable::traverse_the_history(TraversableSessionHistory::TraversalTarget const& target)
{
    VERIFY(m_running_traversal_operation.has_value());
    auto& operation = *m_running_traversal_operation;

    // FIXME: Top-level process replacement is still handled by the existing load-and-seed recovery path. Once
    // cross-document finalization is admitted to the canonical queue, placement can be selected per changing job.
    auto will_replace_web_content_process = SiteIsolationManager::the().navigation_requires_process_swap(operation.current_url, target.target_top_level_entry->url);
    auto webdriver_pending_navigation_completes_with_session_history_update = false;
    if (auto const* current_entry = m_session_history.current_entry())
        webdriver_pending_navigation_completes_with_session_history_update = current_entry->document_state.id == target.target_top_level_entry->document_state.id;
    operation.target_step = target.target_step;
    operation.target_step_index = target.target_step_index;
    operation.will_change_top_level_entry = target.changes_top_level_entry;
    operation.will_replace_web_content_process = will_replace_web_content_process;
    operation.webdriver_pending_navigation_completes_with_session_history_update = webdriver_pending_navigation_completes_with_session_history_update;

    operation.can_apply_in_current_web_content = !m_pending_web_content_session_history_seed.should_send_entries
            && !m_pending_web_content_session_history_seed.ignore_updates_until_seed
            && !m_pending_web_content_session_history_seed.waiting_for_ack
            && !m_session_history_entry_url_loading_from_ui_process.has_value()
            && !m_pending_web_content_session_history_seed.step_after_loading_top_level_entry.has_value()
        && !will_replace_web_content_process;

    operation.apply_history_step.emplace();
    auto needs_cancelation_check = operation.check_for_cancelation == CheckForCancelation::Yes
        || (operation.check_for_cancelation == CheckForCancelation::IfWebContentCannotTraverseTarget && !operation.can_apply_in_current_web_content);
    operation.stage = needs_cancelation_check ? TraversalOperation::Stage::CheckingCancelation : TraversalOperation::Stage::ApplyingHistoryStep;
    dispatch_history_step_cancelation_job();
        return {
        .outcome = { .status = HistoryTraversalStatus::Started, .will_replace_web_content_process = will_replace_web_content_process, .will_change_top_level_entry = target.changes_top_level_entry, .waiting_for_cancelation_check = needs_cancelation_check },
            .webdriver_pending_navigation_url = target.target_top_level_entry->url,
            .webdriver_pending_navigation_completes_with_session_history_update = webdriver_pending_navigation_completes_with_session_history_update,
        };
    }

static void add_participating_endpoint(CanonicalTraversable::TraversalOperation& operation, CanonicalTraversable::TraversalOperation::Endpoint endpoint)
{
    auto already_participates = operation.participating_endpoints.find_if([&](auto const& existing) {
        return existing.client == endpoint.client && existing.page_id == endpoint.page_id;
    }) != operation.participating_endpoints.end();
    if (!already_participates)
        operation.participating_endpoints.append(move(endpoint));
}

static Optional<CanonicalTraversable::TraversalOperation::Endpoint> endpoint_for_navigable(CanonicalTraversable::TraversalOperation const& operation, CanonicalNavigable& navigable)
{
    if (navigable.is_top_level_traversable())
        return operation.root_endpoint;
    if (navigable.has_remote_host()) {
        return CanonicalTraversable::TraversalOperation::Endpoint {
            .client = navigable.remote_host_client(),
            .page_id = navigable.remote_host_page_id(),
        };
    }
    return CanonicalTraversable::TraversalOperation::Endpoint {
        .client = navigable.reporting_client(),
        .page_id = navigable.reporting_page_id(),
        };
    }

void CanonicalTraversable::dispatch_history_step_cancelation_job()
{
    VERIFY(m_running_traversal_operation.has_value());
    auto& operation = *m_running_traversal_operation;
    VERIFY(operation.apply_history_step.has_value());

    if (!operation.root_endpoint.has_value() || !operation.root_endpoint->client) {
        complete_running_traversal_operation(Web::HTML::HistoryStepResult::CanceledByMissingPage);
        return;
    }

    // 4. Let navigablesCrossingDocuments be the result of getting all navigables that might experience a
    // cross-document traversal given traversable and targetStep.
    auto navigables_crossing_documents = m_session_history.get_all_navigables_that_might_experience_a_cross_document_traversal(*this, operation.target_step);

    // The initiator and its source snapshot live in the requesting process. Before iframe isolation is enabled for
    // this path, that process also hosts the active document tree and can run the complete cancelation job.
    auto endpoint = *operation.root_endpoint;
    if (operation.initiating_endpoint.has_value()
        && (operation.initiating_endpoint->client != endpoint.client || operation.initiating_endpoint->page_id != endpoint.page_id))
        endpoint.initiation_id.clear();
    endpoint.client->async_run_history_step_cancelation_job(
        endpoint.page_id,
        operation.operation_id,
        operation.target_step,
        move(navigables_crossing_documents),
        operation.user_involvement,
        endpoint.initiation_id,
        operation.stage == TraversalOperation::Stage::CheckingCancelation);
}

void CanonicalTraversable::did_receive_history_step_cancelation_job_result(WebContentClient& client, u64 page_id, u64 operation_id, Web::HTML::HistoryStepResult result)
{
    if (!m_running_traversal_operation.has_value() || m_running_traversal_operation->operation_id != operation_id) {
        dbgln("Ignoring result for unknown history operation {}", operation_id);
        return;
    }

    auto& operation = *m_running_traversal_operation;
    if (!operation.root_endpoint.has_value()
        || operation.root_endpoint->client.ptr() != &client
        || operation.root_endpoint->page_id != page_id) {
        dbgln("Ignoring history cancelation result from the wrong WebContent host for operation {}", operation_id);
        return;
    }

    if (result == Web::HTML::HistoryStepResult::CanceledPendingNavigation) {
        auto target = m_session_history.traversal_target_for_step(operation.target_step);
        auto const* previous_current_entry = m_pending_session_history_navigation.has_value()
            ? m_pending_session_history_navigation->previous_session_history.current_entry()
            : nullptr;
        if (target.has_value()
            && previous_current_entry
            && m_pending_session_history_navigation->web_content_restore_mode == PendingSessionHistoryNavigation::WebContentRestoreMode::PreserveCurrentProcessState
            && target->target_top_level_entry->document_state.id == previous_current_entry->document_state.id) {
            auto operation_id = operation.operation_id;
            auto on_ready_to_start = move(operation.on_ready_to_start);
            auto on_outcome = move(operation.on_cancelation_check_complete);
            if (on_ready_to_start)
                on_ready_to_start({ .outcome = { .status = HistoryTraversalStatus::Started }, .action = HistoryTraversalAction::RestorePendingNavigation });
            if (m_running_traversal_operation.has_value() && m_running_traversal_operation->operation_id == operation_id)
                complete_running_traversal_operation(Web::HTML::HistoryStepResult::Applied);
            if (on_outcome)
                on_outcome({ .status = HistoryTraversalStatus::Started });
            return;
        }
        result = Web::HTML::HistoryStepResult::Applied;
    }

    if (result != Web::HTML::HistoryStepResult::Applied) {
        if (operation.on_ready_to_start)
            operation.on_ready_to_start({ .outcome = { .status = HistoryTraversalStatus::Canceled }, .action = HistoryTraversalAction::HistoryStepCanceled });
        auto on_outcome = move(operation.on_cancelation_check_complete);
        complete_running_traversal_operation(result);
        if (on_outcome)
            on_outcome({ .status = HistoryTraversalStatus::Canceled });
        return;
    }

    if (!operation.can_apply_in_current_web_content) {
        auto target = m_session_history.traversal_target_for_step(operation.target_step);
        if (!target.has_value()) {
            complete_running_traversal_operation(Web::HTML::HistoryStepResult::CanceledByMissingPage);
            return;
        }
    operation.stage = TraversalOperation::Stage::LoadingEntryFromUIProcess;
        prepare_to_load_session_history_traversal_target_from_ui_process(*target, operation.current_url);
        auto operation_id = operation.operation_id;
        auto root_endpoint = operation.root_endpoint;
        auto on_ready_to_start = move(operation.on_ready_to_start);
        auto on_outcome = move(operation.on_cancelation_check_complete);
        if (on_outcome)
            on_outcome({ .status = HistoryTraversalStatus::Started, .will_replace_web_content_process = operation.will_replace_web_content_process, .will_change_top_level_entry = operation.will_change_top_level_entry });
        if (on_ready_to_start) {
            on_ready_to_start({
                .outcome = { .status = HistoryTraversalStatus::Started, .will_replace_web_content_process = operation.will_replace_web_content_process, .will_change_top_level_entry = operation.will_change_top_level_entry },
        .action = HistoryTraversalAction::LoadCurrentEntryFromUIProcess,
                .webdriver_pending_navigation_url = target->target_top_level_entry->url,
        .webdriver_pending_navigation_completes_with_session_history_update = true,
            });
        }
        // Loading the selected entry is still implemented by navigate(), which occupies the next WebContent-local
        // traversal queue slot. Enqueue that load first, then release this operation's compatibility slot so it can
        // run while the UI operation remains parked until the seed/restore continuation completes.
        if (m_running_traversal_operation.has_value()
            && m_running_traversal_operation->operation_id == operation_id
            && root_endpoint.has_value()
            && root_endpoint->client) {
            root_endpoint->client->async_release_history_operation_local_queue_slot(root_endpoint->page_id, operation_id);
        }
        return;
    }

    auto on_outcome = move(operation.on_cancelation_check_complete);
    if (on_outcome)
        on_outcome({ .status = HistoryTraversalStatus::Started, .will_change_top_level_entry = operation.will_change_top_level_entry });
    if (operation.stage != TraversalOperation::Stage::RestoringNestedStepAfterSeed)
        operation.stage = TraversalOperation::Stage::ApplyingHistoryStep;
    dispatch_changing_navigable_history_jobs();
}

void CanonicalTraversable::dispatch_changing_navigable_history_jobs()
{
    VERIFY(m_running_traversal_operation.has_value());
    auto& operation = *m_running_traversal_operation;
    VERIFY(operation.apply_history_step.has_value());
    auto& apply = *operation.apply_history_step;

    // 6. Let changingNavigables be the result of get all navigables whose current session history entry will change
    // or reload given traversable and targetStep.
    Vector<Web::HTML::CrossProcessId> changing_navigables;
    if (auto const* finalize = operation.requested_traversal.get_pointer<TraversalOperation::FinalizeCrossDocument>()) {
        // AD-HOC: A cross-document finalization changed exactly one navigable's entry, and the canonical comparison
        //         below cannot see that change because the committed entry has already replaced the current one.
        changing_navigables.append(finalize->navigable_id);
    } else if (operation.stage == TraversalOperation::Stage::RestoringNestedStepAfterSeed
        && apply.web_content_current_step == operation.target_step) {
        // A seed installs the canonical current step before the live child navigables have necessarily restored their
        // current entries. Conservatively assign a job to each live navigable; WebContent filters out navigables whose
        // target entry is already current. This preserves the specification's per-navigable job boundary while the
        // temporary two-phase load-then-seed reconstruction exists.
        for_each_in_inclusive_subtree([&](CanonicalNavigable& navigable) {
            changing_navigables.append(navigable.id());
            return IterationDecision::Continue;
        });
    } else {
        changing_navigables = m_session_history.get_all_navigables_whose_current_session_history_entry_will_change_or_reload(*this, operation.target_step, apply.web_content_current_step);
    }

    // 7. Let nonchangingNavigablesThatStillNeedUpdates be the result of getting all navigables that only need history
    // object length/index update given traversable and targetStep.
    apply.nonchanging_navigables_that_still_need_updates = m_session_history.get_all_navigables_that_only_need_history_object_length_index_update(*this, operation.target_step, apply.web_content_current_step);
    // The canonical query above cannot classify the finalized navigable (its committed entry is already current);
    // it belongs to the changing set assembled explicitly.
    apply.nonchanging_navigables_that_still_need_updates.remove_all_matching([&](auto navigable_id) {
        return changing_navigables.contains_slow(navigable_id);
    });
    // 8. For each navigable of changingNavigables:
    for (auto navigable_id : changing_navigables) {
        auto navigable = find(navigable_id);
        if (!navigable.has_value())
            continue;
        auto const* target_entry = m_session_history.get_the_target_history_entry(*navigable, operation.target_step);
        auto endpoint = endpoint_for_navigable(operation, *navigable);
        if (!target_entry || !endpoint.has_value() || !endpoint->client)
            continue;

        apply.changing_navigable_jobs.append({
            .navigable_id = navigable_id,
            .target_entry = *target_entry,
            .client = endpoint->client,
            .page_id = endpoint->page_id,
        });
        add_participating_endpoint(operation, *endpoint);

        auto initiation_id = operation.initiating_endpoint.has_value()
                && operation.initiating_endpoint->client == endpoint->client
                && operation.initiating_endpoint->page_id == endpoint->page_id
            ? operation.initiating_endpoint->initiation_id
            : Optional<u64> { };
        endpoint->client->async_run_changing_navigable_history_job(
            endpoint->page_id, operation.operation_id, navigable_id, operation.target_step, *target_entry,
            operation.user_involvement, initiation_id);
    }

    if (apply.changing_navigable_jobs.is_empty())
        finish_applying_history_step();
}

void CanonicalTraversable::did_receive_changing_navigable_history_job_ready(WebContentClient& client, u64 page_id, u64 operation_id, Web::HTML::CrossProcessId navigable_id, Web::ChangingNavigableHistoryStepJobDisposition disposition)
{
    if (!m_running_traversal_operation.has_value() || m_running_traversal_operation->operation_id != operation_id) {
        dbgln("Ignoring changing-navigable result for unknown history operation {}", operation_id);
        return;
    }
    auto& operation = *m_running_traversal_operation;
    if (!operation.apply_history_step.has_value())
        return;
    auto& apply = *operation.apply_history_step;
    auto job = apply.changing_navigable_jobs.find_if([&](auto const& candidate) {
        return candidate.navigable_id == navigable_id && candidate.client.ptr() == &client && candidate.page_id == page_id;
    });
    if (job == apply.changing_navigable_jobs.end() || job->completed) {
        dbgln("Ignoring changing-navigable result from the wrong WebContent host for operation {}", operation_id);
        return;
    }

    job->completed = true;
    ++apply.completed_changing_navigable_jobs;
    if (history_debug_enabled())
        dbgln("[History] operation {} job ready navigable={} disposition={}", operation_id, navigable_id, (int)disposition);
    if (disposition == Web::ChangingNavigableHistoryStepJobDisposition::Ready) {
        apply.ready_continuations.append(navigable_id);
    } else if (disposition == Web::ChangingNavigableHistoryStepJobDisposition::Stale) {
        complete_running_traversal_operation(Web::HTML::HistoryStepResult::CanceledByMissingPage);
        return;
    }

    if (apply.completed_changing_navigable_jobs == apply.changing_navigable_jobs.size())
        process_changing_navigable_continuations();
}

void CanonicalTraversable::process_changing_navigable_continuations()
{
    VERIFY(m_running_traversal_operation.has_value());
    auto& operation = *m_running_traversal_operation;
    VERIFY(operation.apply_history_step.has_value());
    auto& apply = *operation.apply_history_step;
    if (apply.continuation_in_flight.has_value())
        return;

    while (run_first_queued_synchronous_navigation_steps_not_targeting(apply.navigables_that_must_wait_before_handling_sync_navigation)) {
    }

    if (apply.ready_continuations.is_empty()) {
        finish_applying_history_step();
        return;
    }

    auto navigable_id = apply.ready_continuations.take_first();
    auto navigable = find(navigable_id);
    auto job = apply.changing_navigable_jobs.find_if([&](auto const& candidate) { return candidate.navigable_id == navigable_id; });
    auto length_and_index = m_session_history.get_the_history_object_length_and_index(operation.target_step);
    if (!navigable.has_value() || job == apply.changing_navigable_jobs.end() || !length_and_index.has_value()) {
        process_changing_navigable_continuations();
        return;
    }

    // 9. Let entriesForNavigationAPI be the result of getting session history entries for the navigation API given
    // navigable and targetStep.
    auto entries_for_navigation_api = m_session_history.get_session_history_entries_for_the_navigation_api(*navigable, operation.target_step);
    if (!entries_for_navigation_api.has_value()) {
        complete_running_traversal_operation(Web::HTML::HistoryStepResult::CanceledByMissingPage);
        return;
    }

    // Append navigable to navigablesThatMustWaitBeforeHandlingSyncNavigation.
    apply.navigables_that_must_wait_before_handling_sync_navigation.set(navigable_id);
    apply.continuation_in_flight = navigable_id;
    job->client->async_apply_changing_navigable_continuation(
        job->page_id, operation.operation_id, navigable_id,
        length_and_index->script_history_length, length_and_index->script_history_index,
        move(*entries_for_navigation_api));
}

void CanonicalTraversable::did_apply_changing_navigable_continuation(WebContentClient& client, u64 page_id, u64 operation_id, Web::HTML::CrossProcessId navigable_id)
{
    if (!m_running_traversal_operation.has_value() || m_running_traversal_operation->operation_id != operation_id)
        return;
    auto& operation = *m_running_traversal_operation;
    if (!operation.apply_history_step.has_value())
        return;
    auto& apply = *operation.apply_history_step;
    auto job = apply.changing_navigable_jobs.find_if([&](auto const& candidate) {
        return candidate.navigable_id == navigable_id && candidate.client.ptr() == &client && candidate.page_id == page_id;
    });
    if (job == apply.changing_navigable_jobs.end() || apply.continuation_in_flight != navigable_id)
        return;

    apply.continuation_in_flight.clear();
    process_changing_navigable_continuations();
}

void CanonicalTraversable::finish_applying_history_step()
{
    VERIFY(m_running_traversal_operation.has_value());
    auto& operation = *m_running_traversal_operation;
    VERIFY(operation.apply_history_step.has_value());
    auto& apply = *operation.apply_history_step;
    auto length_and_index = m_session_history.get_the_history_object_length_and_index(operation.target_step);
    if (!length_and_index.has_value()) {
        complete_running_traversal_operation(Web::HTML::HistoryStepResult::CanceledByMissingPage);
        return;
    }

    // 18. For each navigable of nonchangingNavigablesThatStillNeedUpdates, queue a global task on the navigation and
    // traversal task source to update its History object. The specification does not wait for these tasks.
    for (auto navigable_id : apply.nonchanging_navigables_that_still_need_updates) {
        auto navigable = find(navigable_id);
        if (!navigable.has_value())
            continue;
        auto endpoint = endpoint_for_navigable(operation, *navigable);
        if (!endpoint.has_value() || !endpoint->client)
            continue;
        add_participating_endpoint(operation, *endpoint);
        endpoint->client->async_update_nonchanging_navigable_history_state(
            endpoint->page_id, operation.operation_id, navigable_id,
            length_and_index->script_history_length, length_and_index->script_history_index);
    }

    // 20. Set traversable's current session history step to targetStep.
    if (!m_session_history.set_current_session_history_step(operation.target_step)) {
        complete_running_traversal_operation(Web::HTML::HistoryStepResult::CanceledByMissingPage);
        return;
    }
    if (m_pending_web_content_session_history_seed.step_after_loading_top_level_entry == operation.target_step)
        m_pending_web_content_session_history_seed.step_after_loading_top_level_entry.clear();
    m_current_web_content_session_history_matches_mirror = m_session_history.web_content_history_matches_mirror();

    Optional<URL::URL> current_url;
    if (auto const* current_entry = m_session_history.current_entry())
        current_url = current_entry->url;
    if (operation.on_ready_to_start) {
        operation.on_ready_to_start({
            .outcome = { .status = HistoryTraversalStatus::Started, .will_change_top_level_entry = operation.will_change_top_level_entry },
            .action = HistoryTraversalAction::HistoryStepApplied,
            .webdriver_pending_navigation_url = move(current_url),
            .webdriver_pending_navigation_completes_with_session_history_update = operation.webdriver_pending_navigation_completes_with_session_history_update,
        });
    }
    complete_running_traversal_operation(Web::HTML::HistoryStepResult::Applied, operation.target_step);
}

void CanonicalTraversable::continue_history_operation_after_web_content_seed(WebContentClient& client, u64 page_id, i32 step)
{
    if (!m_running_traversal_operation.has_value() || m_running_traversal_operation->target_step != step)
        return;
    auto& operation = *m_running_traversal_operation;
    operation.root_endpoint = TraversalOperation::Endpoint { .client = client, .page_id = page_id };
    add_participating_endpoint(operation, *operation.root_endpoint);
    operation.can_apply_in_current_web_content = true;
    operation.stage = TraversalOperation::Stage::RestoringNestedStepAfterSeed;
    operation.apply_history_step.emplace();
    operation.apply_history_step->web_content_current_step = m_session_history.web_content_current_step();
    dispatch_history_step_cancelation_job();
}

URL::URL CanonicalTraversable::prepare_to_load_session_history_traversal_target_from_ui_process(TraversableSessionHistory::TraversalTarget const& target, URL::URL const& current_url)
{
    if (!m_running_traversal_operation.has_value() || m_running_traversal_operation->target_step != target.target_step) {
        if (m_running_traversal_operation.has_value())
            complete_running_traversal_operation();
        m_running_traversal_operation = TraversalOperation {
            .requested_traversal = TraversalOperation::ToStep { target.target_step },
            .check_for_cancelation = CheckForCancelation::No,
            .current_url = current_url,
            .operation_id = m_next_history_operation_id++,
            .target_step = target.target_step,
            .target_step_index = target.target_step_index,
            .will_change_top_level_entry = target.changes_top_level_entry,
            .will_replace_web_content_process = SiteIsolationManager::the().navigation_requires_process_swap(current_url, target.target_top_level_entry->url),
            .stage = TraversalOperation::Stage::LoadingEntryFromUIProcess,
        };
    } else {
        m_running_traversal_operation->stage = TraversalOperation::Stage::LoadingEntryFromUIProcess;
    }

    auto target_url = target.target_top_level_entry->url;
    auto previous_session_history = m_session_history;
    m_session_history.traverse_to(target.target_step_index);
    prepare_to_seed_web_content_session_history_from_ui_process();
    m_pending_session_history_navigation = PendingSessionHistoryNavigation { target_url, move(previous_session_history) };
    return target_url;
}

Optional<WebContentSessionHistorySeed> CanonicalTraversable::prepare_web_content_session_history_seed(bool allow_current_entry_reconstruction)
{
    auto current_top_level_entry_index = m_session_history.current_top_level_entry_index();
    if (!current_top_level_entry_index.has_value()) {
        abandon_pending_web_content_session_history_seed();
        m_current_web_content_session_history_matches_mirror = false;
        m_session_history.forget_web_content_state();
        return {};
    }

    auto entries = m_session_history.entries();
    if (entries.is_empty()) {
        abandon_pending_web_content_session_history_seed();
        m_current_web_content_session_history_matches_mirror = false;
        m_session_history.forget_web_content_state();
        return {};
    }

    auto is_restoring_traversal_target = m_running_traversal_operation.has_value()
        && (m_running_traversal_operation->stage == TraversalOperation::Stage::LoadingEntryFromUIProcess
            || m_running_traversal_operation->stage == TraversalOperation::Stage::ReplacingWebContentProcess
            || m_running_traversal_operation->stage == TraversalOperation::Stage::RestoringNestedStepAfterSeed);
    auto allow_reconstructing_current_entry = is_restoring_traversal_target
        || m_pending_web_content_session_history_seed.step_after_loading_top_level_entry.has_value()
        || allow_current_entry_reconstruction;

    return WebContentSessionHistorySeed {
        .entries = move(entries),
        .current_top_level_entry_index = *current_top_level_entry_index,
        .allow_current_entry_reconstruction = allow_reconstructing_current_entry,
    };
}

void CanonicalTraversable::did_send_web_content_session_history_seed()
{
    m_pending_web_content_session_history_seed.waiting_for_ack = true;
    m_pending_web_content_session_history_seed.should_send_entries = false;
}

bool CanonicalTraversable::prepare_to_restore_current_session_history_entry_from_ui_process()
{
    auto should_seed = !m_pending_web_content_session_history_seed.step_after_loading_top_level_entry.has_value();
    if (should_seed)
        m_pending_web_content_session_history_seed.should_reseed_after_current_history_load = true;
    return should_seed;
}

CurrentSessionHistoryEntryLoad CanonicalTraversable::prepare_current_session_history_entry_load(URL::URL const& current_url)
{
    auto const* current_entry = m_session_history.current_entry();
    if (!current_entry) {
        m_session_history_entry_url_loading_from_ui_process = current_url;
        return { .url = current_url, .document_resource = Empty {}, .history_handling = Web::Bindings::NavigationHistoryBehavior::Auto };
    }

    m_session_history_entry_url_loading_from_ui_process = current_entry->url;
    auto history_handling = m_pending_web_content_session_history_seed.waiting_for_ack || m_session_history.web_content_uses_ui_step_coordinates()
        ? Web::Bindings::NavigationHistoryBehavior::Replace
        : Web::Bindings::NavigationHistoryBehavior::Auto;
    return { .url = current_entry->url, .document_resource = current_entry->document_state.resource, .history_handling = history_handling };
}

void CanonicalTraversable::did_crash_requiring_web_content_session_history_seed()
{
    // A crashed endpoint cannot finish any document-local jobs it owns. Complete that operation before creating a
    // recovery operation which keeps later traversal queue entries behind reconstruction of the current history step.
    complete_running_traversal_operation(Web::HTML::HistoryStepResult::CanceledByMissingPage);
    m_session_history_entry_url_loading_from_ui_process.clear();
    prepare_to_seed_web_content_session_history_from_ui_process();

    auto current_step = m_session_history.current_step();
    if (!current_step.has_value())
        return;
    auto target = m_session_history.traversal_target_for_step(*current_step);
    if (!target.has_value())
        return;

    m_running_traversal_operation = TraversalOperation {
        .requested_traversal = TraversalOperation::ToStep { *current_step },
        .check_for_cancelation = CheckForCancelation::No,
        .operation_id = m_next_history_operation_id++,
        .target_step = target->target_step,
        .target_step_index = target->target_step_index,
        .will_change_top_level_entry = target->changes_top_level_entry,
        .stage = TraversalOperation::Stage::LoadingEntryFromUIProcess,
    };
}

void CanonicalTraversable::reset_session_history_for_testing(Function<void(u64 operation_id)> start, Function<void()> on_complete)
{
    append_session_history_traversal_operation({
        .requested_traversal = TraversalOperation::ResetForTesting {
            .start = move(start),
            .on_complete = move(on_complete),
        },
        .check_for_cancelation = CheckForCancelation::No,
    });
}

void CanonicalTraversable::did_reset_session_history_for_testing(u64 operation_id, Optional<Web::HTML::SessionHistoryEntryDescriptor> initial_history_entry)
{
    if (!m_running_traversal_operation.has_value()
        || m_running_traversal_operation->operation_id != operation_id
        || !m_running_traversal_operation->requested_traversal.has<TraversalOperation::ResetForTesting>()) {
        dbgln("Ignoring reset result for unknown history operation {}", operation_id);
        return;
    }

    auto on_complete = move(m_running_traversal_operation->requested_traversal.get<TraversalOperation::ResetForTesting>().on_complete);
    m_session_history.clear();
    // Reseed with WebContent's post-reset entry so both processes keep one step coordinate space; an empty canonical
    // history would drop the initial step and skew every later history length and index computation.
    m_current_web_content_session_history_matches_mirror = initial_history_entry.has_value()
        && m_session_history.initialize_with_initial_history_entry(move(*initial_history_entry));
    if (m_current_web_content_session_history_matches_mirror) {
        m_pending_session_history_navigation.clear();
        m_session_history_entry_url_loading_from_ui_process.clear();
        abandon_pending_web_content_session_history_seed();
        complete_running_traversal_operation();
        if (on_complete)
            on_complete();
        return;
    }
    m_current_web_content_session_history_matches_mirror = false;
    m_pending_session_history_navigation.clear();
    m_session_history_entry_url_loading_from_ui_process.clear();
    abandon_pending_web_content_session_history_seed();
    complete_running_traversal_operation();
    if (on_complete)
        on_complete();
}

void CanonicalTraversable::mark_web_content_session_history_stale_for_testing()
{
    m_current_web_content_session_history_matches_mirror = false;
}

StringView CanonicalTraversable::pending_session_history_navigation_web_content_restore_mode_to_string(PendingSessionHistoryNavigation::WebContentRestoreMode mode)
{
    switch (mode) {
    case PendingSessionHistoryNavigation::WebContentRestoreMode::PreserveCurrentProcessState:
        return "preserve-current-process-state"sv;
    case PendingSessionHistoryNavigation::WebContentRestoreMode::RestoreFromUIProcess:
        return "restore-from-ui-process"sv;
    }
    VERIFY_NOT_REACHED();
}

StringView CanonicalTraversable::pending_session_history_traversal_stage_to_string(TraversalOperation::Stage stage)
{
    switch (stage) {
    case TraversalOperation::Stage::ApplyingHistoryStep:
        return "applying-history-step"sv;
    case TraversalOperation::Stage::CheckingCancelation:
        return "checking-cancelation"sv;
    case TraversalOperation::Stage::LoadingEntryFromUIProcess:
        return "loading-entry-from-ui-process"sv;
    case TraversalOperation::Stage::ReplacingWebContentProcess:
        return "replacing-webcontent-process"sv;
    case TraversalOperation::Stage::RestoringNestedStepAfterSeed:
        return "restoring-nested-step-after-seed"sv;
    }
    VERIFY_NOT_REACHED();
}

}
