/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/CanonicalTraversable.h>
#include <LibWebView/SiteIsolationManager.h>
#include <LibWebView/WebContentClient.h>

namespace WebView {

CanonicalTraversable::CanonicalTraversable()
    : CanonicalNavigable({}, {}, nullptr, 0)
{
}

CanonicalNavigable& CanonicalTraversable::insert(WebContentClient& reporting_client, u64 page_id, Web::HTML::NavigableId parent_frame_id, Web::HTML::NavigableId frame_id, CanonicalNavigable& fallback_parent)
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

Optional<CanonicalNavigable&> CanonicalTraversable::find(Web::HTML::NavigableId frame_id)
{
    auto navigable = m_navigable_index.get(frame_id);
    if (!navigable.has_value() || !navigable.value())
        return {};

    return *navigable.value();
}

Optional<CanonicalNavigable const&> CanonicalTraversable::find(Web::HTML::NavigableId frame_id) const
{
    auto navigable = m_navigable_index.get(frame_id);
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
    m_pending_web_content_session_history_seed.clear();
}

void CanonicalTraversable::prepare_to_seed_web_content_session_history_from_ui_process()
{
    prepare_to_seed_web_content_session_history_from_ui_process(m_session_history.current_step_to_restore_after_loading_top_level_entry());
}

void CanonicalTraversable::prepare_to_seed_web_content_session_history_from_ui_process_and_traverse_to_step(i32 step)
{
    prepare_to_seed_web_content_session_history_from_ui_process(step);
}

void CanonicalTraversable::prepare_to_seed_web_content_session_history_from_ui_process(Optional<i32> step_to_traverse_after_seed)
{
    m_current_web_content_session_history_matches_mirror = false;
    m_session_history.forget_web_content_state();
    m_pending_session_history_navigation.clear();
    m_pending_web_content_session_history_seed.clear();
    m_pending_web_content_session_history_seed.step_to_traverse_after_seed = step_to_traverse_after_seed;
    m_pending_web_content_session_history_seed.should_send_entries = true;
    m_pending_web_content_session_history_seed.ignore_updates_until_seed = true;
}

ProcessSwapNavigationPreparation CanonicalTraversable::prepare_for_process_swap_navigation(Web::HTML::CrossProcessNavigationContinuation continuation)
{
    ProcessSwapNavigationPreparation result;
    auto url = continuation.url;

    if (m_pending_session_history_traversal.has_value() && m_pending_session_history_traversal->will_replace_web_content_process)
        m_pending_session_history_traversal->stage = PendingSessionHistoryTraversal::Stage::ReplacingWebContentProcess;

    auto pending_history_handling = continuation.history_handling;
    if (pending_history_handling == Web::Bindings::NavigationHistoryBehavior::Auto) {
        if (auto const* current_entry = m_session_history.current_entry(); current_entry && current_entry->url != url)
            pending_history_handling = Web::Bindings::NavigationHistoryBehavior::Push;
        else if (m_session_history.current_entry())
            pending_history_handling = Web::Bindings::NavigationHistoryBehavior::Replace;
    }
    continuation.history_handling = pending_history_handling;

    m_pending_session_history_navigation = PendingSessionHistoryNavigation {
        .url = url,
        .previous_session_history = m_session_history,
        .web_content_restore_mode = PendingSessionHistoryNavigation::WebContentRestoreMode::RestoreFromUIProcess,
        .document_resource = continuation.document_resource,
        .history_handling = pending_history_handling,
        .commit_behavior = PendingSessionHistoryNavigation::CommitBehavior::CommitFromWebContent,
        .cross_process_navigation_continuation = move(continuation),
    };

    m_session_history.forget_web_content_state();
    m_pending_web_content_session_history_seed.waiting_for_ack = false;
    m_pending_web_content_session_history_seed.should_send_entries = m_session_history.current_top_level_entry_index().has_value();
    m_pending_web_content_session_history_seed.ignore_updates_until_seed = m_pending_web_content_session_history_seed.should_send_entries;
    result.should_seed_web_content_before_load = m_pending_web_content_session_history_seed.should_send_entries;

    return result;
}

void CanonicalTraversable::prepare_for_page_load()
{
    abandon_pending_web_content_session_history_seed();
    m_pending_session_history_traversal.clear();
    m_pending_session_history_navigation.clear();
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

WebContentSessionHistoryUpdateDecision CanonicalTraversable::did_receive_web_content_session_history_update(Vector<Web::HTML::SessionHistoryEntryDescriptor> entries, Vector<i32> used_steps, size_t current_used_step_index, URL::URL const& current_url)
{
    if (m_pending_web_content_session_history_seed.waiting_for_ack)
        return { .ignore_reason = "ignored-session-history-before-ui-seed-ack"sv };

    auto pending_step_after_seed_was_restored = false;
    if (m_pending_web_content_session_history_seed.step_to_traverse_after_seed.has_value()) {
        if (current_used_step_index >= used_steps.size() || used_steps[current_used_step_index] != *m_pending_web_content_session_history_seed.step_to_traverse_after_seed)
            return { .ignore_reason = "ignored-partial-session-history-before-fallback-seed"sv };
        pending_step_after_seed_was_restored = true;
    }

    if (m_pending_web_content_session_history_seed.ignore_updates_until_seed)
        return { .ignore_reason = "ignored-session-history-before-ui-seed"sv };

    return {
        .update = update_session_history_from_web_content(move(entries), move(used_steps), current_used_step_index, pending_step_after_seed_was_restored, true, current_url),
    };
}

WebContentSessionHistoryUpdateDecision CanonicalTraversable::did_receive_web_content_session_history_update_for_testing(Vector<Web::HTML::SessionHistoryEntryDescriptor> entries, Vector<i32> used_steps, size_t current_used_step_index, URL::URL const& current_url)
{
    // NB: dumpUIProcessSessionHistory() first sends WebContent's current snapshot to the UI process, then returns
    //     the UI mirror. If a stale seed ack is still pending, normal async snapshots are intentionally ignored, so
    //     use the same convergence path as a rejected seed ack to make this testing hook deterministic.
    if (m_pending_web_content_session_history_seed.waiting_for_ack) {
        auto update = adopt_web_content_session_history_after_rejected_seed(move(entries), move(used_steps), current_used_step_index, current_url);
        if (update.update_result == TraversableSessionHistory::UpdateResult::InvalidSnapshot)
            return { .ignore_reason = "ignored-session-history-for-testing-before-ui-seed-ack"sv };
        return { .update = move(update) };
    }

    return {
        .update = update_session_history_from_web_content(move(entries), move(used_steps), current_used_step_index, false, true, current_url),
    };
}

WebContentSessionHistoryUpdateResult CanonicalTraversable::update_session_history_from_web_content(Vector<Web::HTML::SessionHistoryEntryDescriptor> entries, Vector<i32> used_steps, size_t current_used_step_index, bool pending_step_after_seed_was_restored, bool seed_web_content_on_invalid_snapshot, URL::URL const& current_url)
{
    auto entries_for_pending_replace = entries;
    auto used_steps_for_pending_replace = used_steps;
    auto update_result = m_session_history.update_from_web_content(move(entries), move(used_steps), current_used_step_index);
    if (update_result == TraversableSessionHistory::UpdateResult::InvalidSnapshot
        && m_pending_session_history_navigation.has_value()
        && m_pending_session_history_navigation->commit_behavior == PendingSessionHistoryNavigation::CommitBehavior::CommitFromWebContent
        && m_pending_session_history_navigation->history_handling != Web::Bindings::NavigationHistoryBehavior::Push) {
        update_result = m_session_history.update_from_web_content_after_pending_replace(move(entries_for_pending_replace), move(used_steps_for_pending_replace), current_used_step_index);
    }
    m_current_web_content_session_history_matches_mirror = update_result == TraversableSessionHistory::UpdateResult::CompleteSnapshot
        && m_session_history.web_content_history_matches_mirror();

    WebContentSessionHistoryUpdateResult result {
        .update_result = update_result,
    };

    if (update_result != TraversableSessionHistory::UpdateResult::InvalidSnapshot) {
        if (m_pending_session_history_navigation.has_value()
            && m_pending_session_history_navigation->commit_behavior == PendingSessionHistoryNavigation::CommitBehavior::CommitFromWebContent)
            m_pending_session_history_navigation.clear();
        if (update_result == TraversableSessionHistory::UpdateResult::CompleteSnapshot)
            m_pending_session_history_navigation.clear();
        if (auto* current_entry = m_session_history.current_entry())
            result.current_url = current_entry->url;
        if (pending_step_after_seed_was_restored)
            m_pending_web_content_session_history_seed.step_to_traverse_after_seed.clear();
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
    m_pending_session_history_traversal.clear();
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
        m_pending_session_history_traversal.clear();
        result.dump_reason = "webcontent-session-history-seed-rejected"sv;
        return result;
    }

    if (!m_session_history.did_seed_web_content_from_ui_process(move(entries), move(used_steps), current_used_step_index)) {
        abandon_pending_web_content_session_history_seed();
        m_current_web_content_session_history_matches_mirror = false;
        m_session_history.forget_web_content_state();
        m_pending_session_history_traversal.clear();
        result.dump_reason = "webcontent-session-history-seed-ack-mismatch"sv;
        return result;
    }

    m_pending_web_content_session_history_seed.waiting_for_ack = false;
    m_pending_web_content_session_history_seed.ignore_updates_until_seed = false;
    m_current_web_content_session_history_matches_mirror = !m_pending_web_content_session_history_seed.step_to_traverse_after_seed.has_value()
        && !m_pending_session_history_navigation.has_value();
    if (m_pending_web_content_session_history_seed.step_to_traverse_after_seed.has_value()) {
        if (m_pending_session_history_traversal.has_value())
            m_pending_session_history_traversal->stage = PendingSessionHistoryTraversal::Stage::ApplyingSeededHistoryStep;
        result.step_to_traverse = *m_pending_web_content_session_history_seed.step_to_traverse_after_seed;
    } else {
        auto is_waiting_for_history_step_cancelation_check = m_pending_session_history_traversal.has_value()
            && m_pending_session_history_traversal->stage == PendingSessionHistoryTraversal::Stage::CheckingCancelation;
        if (!is_waiting_for_history_step_cancelation_check) {
            m_pending_session_history_traversal.clear();
            result.should_load_pending_navigation = m_pending_session_history_navigation.has_value()
                && m_pending_session_history_navigation->commit_behavior == PendingSessionHistoryNavigation::CommitBehavior::CommitFromWebContent;
            result.should_complete_webdriver_pending_navigation = !m_pending_session_history_navigation.has_value();
        }
    }

    result.dump_reason = "webcontent-session-history-seed-ack"sv;
    return result;
}

NavigationStartResult CanonicalTraversable::did_start_navigation(URL::URL const& url, Variant<Empty, String, Web::HTML::POSTResource> document_resource, bool is_redirect, Web::Bindings::NavigationHistoryBehavior history_handling, bool is_showing_crash_page)
{
    if (m_pending_web_content_session_history_seed.should_send_entries || m_pending_web_content_session_history_seed.ignore_updates_until_seed || m_pending_web_content_session_history_seed.waiting_for_ack) {
        if (auto const* current_entry = m_session_history.current_entry(); current_entry && current_entry->url != url)
            return { .dump_reason = "ignored-navigation-start-before-ui-history-seed"sv };
    }

    if (m_pending_session_history_navigation.has_value()
        && m_pending_session_history_navigation->commit_behavior == PendingSessionHistoryNavigation::CommitBehavior::CommitFromWebContent
        && !is_redirect
        && m_pending_session_history_navigation->url != url) {
        return { .dump_reason = "ignored-navigation-start-during-pending-session-history-navigation"sv, .did_clear_crash_page = is_showing_crash_page };
    }

    if (m_pending_session_history_navigation.has_value()
        && m_pending_session_history_navigation->commit_behavior == PendingSessionHistoryNavigation::CommitBehavior::CommitFromWebContent
        && !is_redirect
        && m_pending_session_history_navigation->url == url) {
        m_pending_session_history_navigation->document_resource = move(document_resource);
        m_pending_session_history_navigation->history_handling = history_handling;
        return { .dump_reason = "did-start-pending-session-history-navigation"sv, .did_clear_crash_page = is_showing_crash_page };
    }

    if (is_showing_crash_page) {
        if (auto const* current_entry = m_session_history.current_entry(); current_entry && current_entry->url == url) {
            prepare_to_seed_web_content_session_history_from_ui_process();
            return { .dump_reason = "did-start-navigation-from-crash-page"sv, .did_clear_crash_page = true };
        }
    }

    if (is_redirect) {
        if (m_pending_session_history_navigation.has_value()) {
            m_pending_session_history_navigation->url = url;
            m_pending_session_history_navigation->document_resource = move(document_resource);
            m_pending_session_history_navigation->history_handling = history_handling;
        }
        return { .dump_reason = "did-start-navigation-redirect"sv, .should_update_webdriver_pending_navigation_url = true, .did_clear_crash_page = is_showing_crash_page };
    }

    m_pending_session_history_navigation = PendingSessionHistoryNavigation {
        .url = url,
        .previous_session_history = m_session_history,
        .web_content_restore_mode = PendingSessionHistoryNavigation::WebContentRestoreMode::PreserveCurrentProcessState,
        .document_resource = move(document_resource),
        .history_handling = history_handling,
        .commit_behavior = PendingSessionHistoryNavigation::CommitBehavior::CommitFromWebContent,
    };
    return { .dump_reason = "did-start-navigation"sv, .did_clear_crash_page = is_showing_crash_page };
}

NavigationCancelResult CanonicalTraversable::did_cancel_navigation(URL::URL const& url, bool has_webdriver_pending_navigation)
{
    if (m_pending_session_history_navigation.has_value() && m_pending_session_history_navigation->url == url) {
        if (m_pending_session_history_navigation->commit_behavior == PendingSessionHistoryNavigation::CommitBehavior::RestorePreviousOnCancel)
            return { .status = NavigationCancelStatus::RestorePendingSessionHistoryNavigation };

        m_pending_session_history_navigation.clear();
        Optional<URL::URL> current_url;
        if (auto const* current_entry = m_session_history.current_entry())
            current_url = current_entry->url;
        return { .status = NavigationCancelStatus::CompleteWebdriverPendingNavigation, .current_url = move(current_url) };
    }

    if (has_webdriver_pending_navigation) {
        m_session_history.clear_current_entry_reload_pending();
        return { .status = NavigationCancelStatus::CompleteWebdriverPendingNavigation };
    }

    return {};
}

NavigationFinishResult CanonicalTraversable::did_finish_navigation(URL::URL const& url)
{
    if (m_pending_session_history_navigation.has_value()
        && m_pending_session_history_navigation->url == url
        && m_pending_session_history_navigation->commit_behavior == PendingSessionHistoryNavigation::CommitBehavior::RestorePreviousOnCancel)
        m_pending_session_history_navigation.clear();

    if (!m_pending_web_content_session_history_seed.should_send_entries)
        return {};

    if (auto const* current_entry = m_session_history.current_entry(); current_entry && current_entry->url == url) {
        m_session_history.clear_current_entry_reload_pending();
        return { .should_seed_web_content = true };
    }

    // NB: The first finish notification from a fresh WebContent process can still report about:blank before the
    //     traversed-to entry is ready. Keep the pending seed state intact so partial snapshots remain ignored
    //     until we can seed the full UI-owned history.
    return { .dump_reason = "skip-seed-webcontent-session-history"sv };
}

RestorePendingSessionHistoryNavigationResult CanonicalTraversable::restore_pending_session_history_navigation()
{
    if (!m_pending_session_history_navigation.has_value())
        return {};

    auto web_content_restore_mode = m_pending_session_history_navigation->web_content_restore_mode;
    m_session_history = move(m_pending_session_history_navigation->previous_session_history);
    m_pending_session_history_navigation.clear();
    m_pending_session_history_traversal.clear();

    RestorePendingSessionHistoryNavigationResult result { .restored = true, .web_content_restore_mode = web_content_restore_mode };
    if (auto* current_entry = m_session_history.current_entry()) {
        result.current_url = current_entry->url;
        if (web_content_restore_mode == PendingSessionHistoryNavigation::WebContentRestoreMode::PreserveCurrentProcessState) {
            abandon_pending_web_content_session_history_seed();
            m_current_web_content_session_history_matches_mirror = m_session_history.web_content_history_matches_mirror();
        } else if (auto current_step = m_session_history.current_step(); current_step.has_value()) {
            prepare_to_seed_web_content_session_history_from_ui_process_and_traverse_to_step(*current_step);
        } else {
            prepare_to_seed_web_content_session_history_from_ui_process();
        }
    } else {
        m_current_web_content_session_history_matches_mirror = false;
    }
    return result;
}

HistoryTraversalDecision CanonicalTraversable::traverse_the_history_by_delta(int delta, CheckForCancelation check_for_cancelation, URL::URL const& current_url, Function<void(HistoryTraversalOutcome)> on_cancelation_check_complete)
{
    auto target = m_session_history.traversal_target_for_delta(delta);
    auto current_url_for_process_selection = current_url;

    if (m_pending_session_history_traversal.has_value()) {
        if (auto pending_target = m_session_history.traversal_target_for_step(m_pending_session_history_traversal->target_step); pending_target.has_value()) {
            target = m_session_history.traversal_target_for_delta_from_step(pending_target->target_step, delta);
            current_url_for_process_selection = pending_target->target_top_level_entry->url;
        } else {
            target.clear();
        }
    }

    if (!target.has_value())
        return { .outcome = { .status = HistoryTraversalStatus::NoEntry } };

    // FIXME: This pre-flight prediction exists only because WebContent applies the history step itself, so the UI must
    //        choose between delegating the traversal to the current process and driving a cross-process load before
    //        sending anything. Once the UI process owns apply-the-history-step and issues per-navigable load commands,
    //        placement is decided per command and this prediction goes away.
    auto will_replace_web_content_process = SiteIsolationManager::the().navigation_requires_process_swap(current_url_for_process_selection, target->target_top_level_entry->url);
    auto pending_traversal = PendingSessionHistoryTraversal {
        .target_step = target->target_step,
        .target_step_index = target->target_step_index,
        .will_change_top_level_entry = target->changes_top_level_entry,
        .will_replace_web_content_process = will_replace_web_content_process,
        .on_cancelation_check_complete = nullptr,
    };

    auto web_content_can_apply_traversal = !m_pending_web_content_session_history_seed.should_send_entries
        && !m_pending_web_content_session_history_seed.ignore_updates_until_seed
        && !m_pending_web_content_session_history_seed.waiting_for_ack
        && !m_pending_session_history_navigation.has_value()
        && !m_pending_web_content_session_history_seed.step_to_traverse_after_seed.has_value()
        && m_session_history.web_content_can_traverse_to(*target);

    if (web_content_can_apply_traversal && !will_replace_web_content_process) {
        m_pending_session_history_traversal = move(pending_traversal);
        auto webdriver_pending_navigation_completes_with_session_history_update = false;
        if (auto const* current_entry = m_session_history.current_entry()) {
            webdriver_pending_navigation_completes_with_session_history_update = current_entry->document_state.id != 0
                && current_entry->document_state.id == target->target_top_level_entry->document_state.id;
        }
        return {
            .outcome = { .status = HistoryTraversalStatus::Started, .will_replace_web_content_process = will_replace_web_content_process, .will_change_top_level_entry = target->changes_top_level_entry },
            .action = HistoryTraversalAction::TraverseInWebContent,
            .target_step = target->target_step,
            .webdriver_pending_navigation_url = target->target_top_level_entry->url,
            .webdriver_pending_navigation_completes_with_session_history_update = webdriver_pending_navigation_completes_with_session_history_update,
        };
    }

    auto needs_cancelation_check = check_for_cancelation == CheckForCancelation::Yes
        || (check_for_cancelation == CheckForCancelation::IfWebContentCannotTraverseTarget && !web_content_can_apply_traversal);
    if (needs_cancelation_check) {
        pending_traversal.stage = PendingSessionHistoryTraversal::Stage::CheckingCancelation;
        pending_traversal.cancelation_check_request_id = m_next_traverse_history_step_cancelation_check_request_id++;
        pending_traversal.on_cancelation_check_complete = move(on_cancelation_check_complete);
        auto request_id = pending_traversal.cancelation_check_request_id;
        m_pending_session_history_traversal = move(pending_traversal);
        return {
            .outcome = { .status = HistoryTraversalStatus::Started, .will_replace_web_content_process = will_replace_web_content_process, .will_change_top_level_entry = target->changes_top_level_entry, .waiting_for_cancelation_check = true },
            .action = HistoryTraversalAction::CheckForCancelation,
            .target_step = target->target_step,
            .cancelation_check_request_id = request_id,
        };
    }

    pending_traversal.stage = PendingSessionHistoryTraversal::Stage::SeedingHistoryFromUIProcess;
    m_pending_session_history_traversal = move(pending_traversal);
    prepare_to_seed_session_history_and_traverse_to_step_from_ui_process(*target, current_url);
    return {
        .outcome = { .status = HistoryTraversalStatus::Started, .will_replace_web_content_process = will_replace_web_content_process, .will_change_top_level_entry = target->changes_top_level_entry },
        .action = HistoryTraversalAction::SeedHistoryAndTraverseInWebContent,
        .webdriver_pending_navigation_url = target->target_top_level_entry->url,
        .webdriver_pending_navigation_completes_with_session_history_update = true,
    };
}

void CanonicalTraversable::ensure_pending_session_history_traversal(TraversableSessionHistory::TraversalTarget const& target, URL::URL const& current_url, PendingSessionHistoryTraversal::Stage stage)
{
    if (!m_pending_session_history_traversal.has_value() || m_pending_session_history_traversal->target_step != target.target_step) {
        m_pending_session_history_traversal = PendingSessionHistoryTraversal {
            .target_step = target.target_step,
            .target_step_index = target.target_step_index,
            .will_change_top_level_entry = target.changes_top_level_entry,
            .will_replace_web_content_process = SiteIsolationManager::the().navigation_requires_process_swap(current_url, target.target_top_level_entry->url),
            .stage = stage,
            .on_cancelation_check_complete = nullptr,
        };
        return;
    }

    m_pending_session_history_traversal->target_step_index = target.target_step_index;
    m_pending_session_history_traversal->will_change_top_level_entry = target.changes_top_level_entry;
    m_pending_session_history_traversal->stage = stage;
}

URL::URL CanonicalTraversable::prepare_to_seed_session_history_and_traverse_to_step_from_ui_process(TraversableSessionHistory::TraversalTarget const& target, URL::URL const& current_url)
{
    ensure_pending_session_history_traversal(target, current_url, PendingSessionHistoryTraversal::Stage::SeedingHistoryFromUIProcess);

    auto target_url = target.target_top_level_entry->url;
    prepare_to_seed_web_content_session_history_from_ui_process_and_traverse_to_step(target.target_step);
    return target_url;
}

WebContentHistoryStepResult CanonicalTraversable::did_traverse_the_history_to_step(i32 step, bool step_was_available, Web::HTML::HistoryStepResult result)
{
    if (!m_pending_web_content_session_history_seed.step_to_traverse_after_seed.has_value()) {
        if (!m_pending_session_history_traversal.has_value() || m_pending_session_history_traversal->target_step != step)
            return { .dump_reason = "ignored-stale-webcontent-history-step-result"sv };

        if (!step_was_available) {
            auto target = m_session_history.traversal_target_for_step(step);
            if (target.has_value())
                return { .dump_reason = "webcontent-history-step-unavailable-fallback-load"sv, .fallback_target = *target };
            m_current_web_content_session_history_matches_mirror = false;
            m_session_history.forget_web_content_state();
            m_pending_session_history_traversal.clear();
            return { .dump_reason = "webcontent-history-step-unavailable"sv, .should_update_navigation_action_state = true };
        }

        if (result != Web::HTML::HistoryStepResult::Applied) {
            m_pending_session_history_traversal.clear();
            return { .dump_reason = "webcontent-history-step-canceled"sv, .should_update_navigation_action_state = true, .should_complete_webdriver_pending_navigation = true, .should_update_webdriver_pending_navigation_to_current_url = true, .should_reset_webdriver_pending_navigation_completion = true };
        }

        if (!m_session_history.did_apply_web_content_traversal_to_step(step)) {
            if (auto target = m_session_history.traversal_target_for_step(step); target.has_value())
                return { .dump_reason = "webcontent-history-step-applied-with-stale-mirror-fallback-load"sv, .fallback_target = *target };
            m_current_web_content_session_history_matches_mirror = false;
            m_session_history.forget_web_content_state();
            m_pending_session_history_traversal.clear();
            return { .dump_reason = "webcontent-history-step-applied-without-ui-target"sv, .should_update_navigation_action_state = true };
        }

        m_current_web_content_session_history_matches_mirror = true;
        auto should_complete_webdriver_pending_navigation = !m_pending_session_history_traversal->will_change_top_level_entry;
        Optional<URL::URL> current_url;
        if (auto const* current_entry = m_session_history.current_entry())
            current_url = current_entry->url;
        m_pending_session_history_traversal.clear();
        return { .dump_reason = "webcontent-history-step-applied"sv, .should_update_navigation_action_state = true, .current_url = move(current_url), .should_complete_webdriver_pending_navigation = should_complete_webdriver_pending_navigation };
    }

    if (*m_pending_web_content_session_history_seed.step_to_traverse_after_seed != step)
        return { .dump_reason = "ignored-stale-webcontent-history-step-result"sv };

    if (step_was_available && result == Web::HTML::HistoryStepResult::Applied) {
        m_pending_web_content_session_history_seed.step_to_traverse_after_seed.clear();
        m_current_web_content_session_history_matches_mirror = m_session_history.did_apply_web_content_traversal_to_step(step);
        Optional<URL::URL> current_url;
        if (auto const* current_entry = m_session_history.current_entry())
            current_url = current_entry->url;
        if (m_pending_session_history_traversal.has_value()
            && m_pending_session_history_traversal->target_step == step
            && m_pending_session_history_traversal->stage == PendingSessionHistoryTraversal::Stage::ApplyingSeededHistoryStep)
            m_pending_session_history_traversal.clear();
        return { .dump_reason = "webcontent-history-step-applied-after-seed"sv, .should_update_navigation_action_state = true, .current_url = move(current_url), .should_complete_webdriver_pending_navigation = true };
    }

    auto pending_step_dump_reason = step_was_available ? "webcontent-pending-history-step-canceled"sv : "webcontent-history-step-unavailable"sv;
    if (step_was_available && result != Web::HTML::HistoryStepResult::Applied && !m_pending_session_history_navigation.has_value()) {
        m_pending_web_content_session_history_seed.step_to_traverse_after_seed.clear();
        m_current_web_content_session_history_matches_mirror = false;
        m_session_history.forget_web_content_state();
        m_pending_session_history_traversal.clear();
        return { .dump_reason = pending_step_dump_reason, .should_update_navigation_action_state = true, .should_complete_webdriver_pending_navigation = true, .should_update_webdriver_pending_navigation_to_current_url = true, .should_reset_webdriver_pending_navigation_completion = true };
    }

    if (m_pending_session_history_navigation.has_value())
        return { .dump_reason = pending_step_dump_reason, .should_restore_pending_navigation = true };

    m_pending_web_content_session_history_seed.step_to_traverse_after_seed.clear();
    m_current_web_content_session_history_matches_mirror = false;
    m_session_history.forget_web_content_state();
    m_pending_session_history_traversal.clear();
    return { .dump_reason = pending_step_dump_reason, .should_update_navigation_action_state = true };
}

HistoryStepCancelationCheckResult CanonicalTraversable::did_check_if_traverse_history_step_is_canceled(u64 request_id, i32 step, bool canceled)
{
    if (!m_pending_session_history_traversal.has_value()
        || m_pending_session_history_traversal->stage != PendingSessionHistoryTraversal::Stage::CheckingCancelation
        || m_pending_session_history_traversal->cancelation_check_request_id != request_id
        || m_pending_session_history_traversal->target_step != step)
        return { .dump_reason = "ignored-stale-history-step-cancelation-check-result"sv };

    if (canceled) {
        auto on_cancelation_check_complete = move(m_pending_session_history_traversal->on_cancelation_check_complete);
        m_pending_session_history_traversal.clear();
        return { .dump_reason = "traverse-fallback-canceled-by-webcontent"sv, .on_cancelation_check_complete = move(on_cancelation_check_complete), .outcome = { .status = HistoryTraversalStatus::Canceled }, .should_update_navigation_action_state = true, .should_complete_webdriver_pending_navigation = true, .should_update_webdriver_pending_navigation_to_current_url = true, .should_reset_webdriver_pending_navigation_completion = true };
    }

    auto target = m_session_history.traversal_target_for_step(step);
    if (!target.has_value()) {
        auto on_cancelation_check_complete = move(m_pending_session_history_traversal->on_cancelation_check_complete);
        m_current_web_content_session_history_matches_mirror = false;
        m_session_history.forget_web_content_state();
        m_pending_session_history_traversal.clear();
        return { .dump_reason = "traverse-fallback-cancelation-check-without-ui-target"sv, .on_cancelation_check_complete = move(on_cancelation_check_complete), .outcome = { .status = HistoryTraversalStatus::NoEntry }, .should_update_navigation_action_state = true };
    }

    auto on_cancelation_check_complete = move(m_pending_session_history_traversal->on_cancelation_check_complete);
    return {
        .dump_reason = "traverse-fallback-load-after-cancelation-check"sv,
        .on_cancelation_check_complete = move(on_cancelation_check_complete),
        .outcome = { .status = HistoryTraversalStatus::Started, .will_replace_web_content_process = m_pending_session_history_traversal->will_replace_web_content_process, .will_change_top_level_entry = m_pending_session_history_traversal->will_change_top_level_entry },
        .target = *target,
    };
}

Optional<WebContentSessionHistorySeed> CanonicalTraversable::prepare_web_content_session_history_seed()
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

    return WebContentSessionHistorySeed {
        .entries = move(entries),
        .current_top_level_entry_index = *current_top_level_entry_index,
    };
}

void CanonicalTraversable::did_send_web_content_session_history_seed()
{
    m_pending_web_content_session_history_seed.waiting_for_ack = true;
    m_pending_web_content_session_history_seed.should_send_entries = false;
}

Optional<CurrentSessionHistoryEntryLoad> CanonicalTraversable::pending_session_history_navigation_load() const
{
    if (!m_pending_session_history_navigation.has_value())
        return {};

    return CurrentSessionHistoryEntryLoad {
        .url = m_pending_session_history_navigation->url,
        .document_resource = m_pending_session_history_navigation->document_resource,
        .history_handling = m_pending_session_history_navigation->history_handling,
        .cross_process_navigation_continuation = m_pending_session_history_navigation->cross_process_navigation_continuation,
    };
}

void CanonicalTraversable::prepare_for_web_content_crash_recovery()
{
    if (m_pending_session_history_traversal.has_value()) {
        auto target = m_session_history.traversal_target_for_step(m_pending_session_history_traversal->target_step);
        if (target.has_value()) {
            m_pending_session_history_traversal->stage = PendingSessionHistoryTraversal::Stage::SeedingHistoryFromUIProcess;
            prepare_to_seed_web_content_session_history_from_ui_process_and_traverse_to_step(m_pending_session_history_traversal->target_step);
            return;
        }
    }

    if (auto current_step = m_session_history.current_step(); current_step.has_value()) {
        prepare_to_seed_web_content_session_history_from_ui_process_and_traverse_to_step(*current_step);
        return;
    }

    prepare_to_seed_web_content_session_history_from_ui_process();
}

void CanonicalTraversable::reset_session_history_for_testing()
{
    m_session_history.clear();
    m_current_web_content_session_history_matches_mirror = false;
    m_pending_session_history_navigation.clear();
    m_pending_session_history_traversal.clear();
    abandon_pending_web_content_session_history_seed();
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

StringView CanonicalTraversable::pending_session_history_traversal_stage_to_string(PendingSessionHistoryTraversal::Stage stage)
{
    switch (stage) {
    case PendingSessionHistoryTraversal::Stage::ApplyingInWebContent:
        return "applying-in-webcontent"sv;
    case PendingSessionHistoryTraversal::Stage::CheckingCancelation:
        return "checking-cancelation"sv;
    case PendingSessionHistoryTraversal::Stage::SeedingHistoryFromUIProcess:
        return "seeding-history-from-ui-process"sv;
    case PendingSessionHistoryTraversal::Stage::ReplacingWebContentProcess:
        return "replacing-webcontent-process"sv;
    case PendingSessionHistoryTraversal::Stage::ApplyingSeededHistoryStep:
        return "applying-seeded-history-step"sv;
    }
    VERIFY_NOT_REACHED();
}

}
