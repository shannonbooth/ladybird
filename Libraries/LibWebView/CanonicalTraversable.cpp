/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/CanonicalTraversable.h>
#include <LibWebView/SiteIsolation.h>

namespace WebView {

CanonicalTraversable::CanonicalTraversable()
{
    auto& root_state = m_root_navigable.replicated_state();
    root_state.is_traversable = true;
    root_state.is_top_level_traversable = true;
}

CanonicalTraversable::~CanonicalTraversable() = default;

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
    // NB: A fresh or repaired WebContent process reaches the current top-level session history entry after loading or
    //     reseeding m_url. If the traversable's current session history step is nested, finish restoration by
    //     traversing to that step after seeding the top-level entries.
    m_pending_web_content_session_history_seed.step_after_loading_top_level_entry = m_session_history.current_step_to_restore_after_loading_top_level_entry();
    m_pending_web_content_session_history_seed.should_send_entries = true;
    m_pending_web_content_session_history_seed.ignore_updates_until_seed = true;
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
        // NB: A complete WebContent snapshot means the UI-owned navigation settled, including redirected navigations
        //     whose final URL differs from the original pending navigation URL. A partial snapshot only updates the UI
        //     mirror with the current WebContent-visible subset.
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
    m_pending_session_history_traversal.clear();
    return update;
}

WebContentSessionHistorySeedAckResult CanonicalTraversable::did_receive_web_content_session_history_seed_ack(bool accepted, Vector<Web::HTML::SessionHistoryEntryDescriptor> entries, Vector<i32> used_steps, size_t current_used_step_index, URL::URL const& current_url)
{
    if (!m_pending_web_content_session_history_seed.waiting_for_ack)
        return {};

    WebContentSessionHistorySeedAckResult result;

    if (!accepted) {
        // NB: WebContent can reject a stale UI-process seed without changing its live session history. In that case
        //     the ack carries WebContent's current snapshot, so converge the UI mirror from the same path used by
        //     normal WebContent history updates. If the snapshot itself is not usable, fall back to forgetting the
        //     WebContent state below and wait for the next repair point.
        auto update = adopt_web_content_session_history_after_rejected_seed(move(entries), move(used_steps), current_used_step_index, current_url);
        if (update.update_result != TraversableSessionHistory::UpdateResult::InvalidSnapshot) {
            result.status = WebContentSessionHistorySeedAckStatus::RejectedWithCurrentSnapshot;
            result.current_url = move(update.current_url);
            return result;
        }

        abandon_pending_web_content_session_history_seed();
        m_current_web_content_session_history_matches_mirror = false;
        m_session_history.forget_web_content_state();
        m_pending_session_history_traversal.clear();
        result.status = WebContentSessionHistorySeedAckStatus::Rejected;
        return result;
    }

    if (!m_session_history.did_seed_web_content_from_ui_process(move(entries), move(used_steps), current_used_step_index)) {
        if (m_pending_web_content_session_history_seed.should_reseed_after_current_history_load) {
            m_pending_web_content_session_history_seed.waiting_for_ack = false;
            m_pending_web_content_session_history_seed.should_send_entries = true;
            m_pending_web_content_session_history_seed.ignore_updates_until_seed = true;
            m_current_web_content_session_history_matches_mirror = false;
            result.status = WebContentSessionHistorySeedAckStatus::PreloadMismatch;
            return result;
        }

        abandon_pending_web_content_session_history_seed();
        m_current_web_content_session_history_matches_mirror = false;
        m_session_history.forget_web_content_state();
        m_pending_session_history_traversal.clear();
        result.status = WebContentSessionHistorySeedAckStatus::Mismatch;
        return result;
    }

    m_pending_web_content_session_history_seed.waiting_for_ack = false;
    if (m_pending_web_content_session_history_seed.should_reseed_after_current_history_load) {
        m_pending_web_content_session_history_seed.should_send_entries = true;
        m_pending_web_content_session_history_seed.ignore_updates_until_seed = true;
        m_current_web_content_session_history_matches_mirror = false;
        result.status = WebContentSessionHistorySeedAckStatus::PreloadAccepted;
        return result;
    }

    m_pending_web_content_session_history_seed.ignore_updates_until_seed = false;
    // NB: A seed ack can arrive while a top-level navigation is still blocked. Keep the mirror provisional until that
    //     navigation settles.
    m_current_web_content_session_history_matches_mirror = !m_pending_web_content_session_history_seed.step_after_loading_top_level_entry.has_value()
        && !m_pending_session_history_navigation.has_value();

    if (m_pending_web_content_session_history_seed.step_after_loading_top_level_entry.has_value()) {
        if (m_pending_session_history_traversal.has_value())
            m_pending_session_history_traversal->stage = PendingSessionHistoryTraversal::Stage::RestoringNestedStepAfterSeed;
        result.step_to_traverse = *m_pending_web_content_session_history_seed.step_after_loading_top_level_entry;
    } else {
        auto is_waiting_for_history_step_cancelation_check = m_pending_session_history_traversal.has_value()
            && m_pending_session_history_traversal->stage == PendingSessionHistoryTraversal::Stage::CheckingCancelation;
        if (!is_waiting_for_history_step_cancelation_check) {
            m_pending_session_history_traversal.clear();
            result.should_complete_webdriver_pending_navigation = !m_pending_session_history_navigation.has_value();
        }
    }

    result.status = WebContentSessionHistorySeedAckStatus::Accepted;
    return result;
}

NavigationStartResult CanonicalTraversable::did_start_navigation(URL::URL const& url, Variant<Empty, String, Web::HTML::POSTResource> document_resource, bool is_redirect, Web::Bindings::NavigationHistoryBehavior history_handling, bool is_showing_crash_page)
{
    if (m_session_history_entry_url_loading_from_ui_process.has_value()) {
        if (*m_session_history_entry_url_loading_from_ui_process != url) {
            // NB: An earlier UI-process history fallback load can start after a newer back/forward request has
            //     superseded it. Chromium, WebKit, and Firefox all give pending history loads an identity so stale
            //     traversals cannot consume state belonging to the current one. Keep the UI-owned history authoritative
            //     here and wait for the load matching the latest requested entry.
            return { .status = NavigationStartStatus::StaleUIHistoryLoad };
        }

        auto should_keep_preseeded_web_content_history = m_pending_web_content_session_history_seed.waiting_for_ack || m_session_history.web_content_uses_ui_step_coordinates();
        m_session_history_entry_url_loading_from_ui_process.clear();
        if (!should_keep_preseeded_web_content_history) {
            m_current_web_content_session_history_matches_mirror = false;
            m_session_history.forget_web_content_state();
        }
        return { .status = NavigationStartStatus::StartedFromUIHistoryLoad };
    }

    if (m_pending_web_content_session_history_seed.should_send_entries || m_pending_web_content_session_history_seed.ignore_updates_until_seed || m_pending_web_content_session_history_seed.waiting_for_ack) {
        if (auto const* current_entry = m_session_history.current_entry(); current_entry && current_entry->url != url)
            return { .status = NavigationStartStatus::IgnoredBeforeUIHistorySeed };
    }

    if (is_showing_crash_page) {
        if (auto const* current_entry = m_session_history.current_entry(); current_entry && current_entry->url == url) {
            prepare_to_seed_web_content_session_history_from_ui_process();
            return {
                .status = NavigationStartStatus::StartedFromCrashPage,
                .did_clear_crash_page = true,
            };
        }
    }

    if (is_redirect) {
        m_session_history.replace_current_entry_url(url);
        if (m_pending_session_history_navigation.has_value())
            m_pending_session_history_navigation->url = url;
        m_current_web_content_session_history_matches_mirror = false;
        return {
            .status = NavigationStartStatus::Redirect,
            .should_update_navigation_action_state = true,
            .should_update_webdriver_pending_navigation_url = true,
            .did_clear_crash_page = is_showing_crash_page,
        };
    }

    if (auto const* current_entry = m_session_history.current_entry(); current_entry && current_entry->url == url) {
        if (m_pending_session_history_navigation.has_value() && m_pending_session_history_navigation->url == url)
            return {
                .status = NavigationStartStatus::CurrentEntryUnchanged,
                .did_clear_crash_page = is_showing_crash_page,
            };

        if (history_handling == Web::Bindings::NavigationHistoryBehavior::Push && m_current_web_content_session_history_matches_mirror)
            m_pending_session_history_navigation = PendingSessionHistoryNavigation { url, m_session_history };
        else
            m_pending_session_history_navigation.clear();

        if (history_handling == Web::Bindings::NavigationHistoryBehavior::Replace) {
            m_session_history.replace_current_entry(url, move(document_resource));
            m_current_web_content_session_history_matches_mirror = false;
            return {
                .status = NavigationStartStatus::ReplacedCurrentEntry,
                .should_update_navigation_action_state = true,
                .did_clear_crash_page = is_showing_crash_page,
            };
        }
        if (history_handling == Web::Bindings::NavigationHistoryBehavior::Push) {
            m_session_history.navigate(url, move(document_resource));
            m_current_web_content_session_history_matches_mirror = false;
            return {
                .status = NavigationStartStatus::PushedCurrentEntry,
                .should_update_navigation_action_state = true,
                .did_clear_crash_page = is_showing_crash_page,
            };
        }
        return {
            .status = NavigationStartStatus::CurrentEntryUnchanged,
            .did_clear_crash_page = is_showing_crash_page,
        };
    }

    if (m_session_history.current_entry())
        m_pending_session_history_navigation = PendingSessionHistoryNavigation { url, m_session_history };
    else
        m_pending_session_history_navigation.clear();
    if (history_handling == Web::Bindings::NavigationHistoryBehavior::Replace)
        m_session_history.replace_current_entry(url, move(document_resource));
    else
        m_session_history.navigate(url, move(document_resource));
    m_current_web_content_session_history_matches_mirror = false;
    return {
        .status = NavigationStartStatus::Started,
        .should_update_navigation_action_state = true,
        .did_clear_crash_page = is_showing_crash_page,
    };
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
    if (m_pending_session_history_navigation.has_value() && m_pending_session_history_navigation->url == url)
        m_pending_session_history_navigation.clear();

    if (!m_pending_web_content_session_history_seed.should_send_entries)
        return {};

    if (auto const* current_entry = m_session_history.current_entry(); current_entry && current_entry->url == url) {
        m_session_history.clear_current_entry_reload_pending();
        auto allow_current_entry_reconstruction = m_pending_web_content_session_history_seed.should_reseed_after_current_history_load;
        m_pending_web_content_session_history_seed.should_reseed_after_current_history_load = false;
        return {
            .should_seed_web_content = true,
            .allow_current_entry_reconstruction = allow_current_entry_reconstruction,
        };
    }

    // NB: The first finish notification from a fresh WebContent process can still report about:blank before the
    //     traversed-to entry is ready. Keep the pending seed state intact so partial snapshots remain ignored
    //     until we can seed the full UI-owned history.
    return { .should_dump_skip_seed = true };
}

RestorePendingSessionHistoryNavigationResult CanonicalTraversable::restore_pending_session_history_navigation()
{
    if (!m_pending_session_history_navigation.has_value())
        return {};

    auto web_content_restore_mode = m_pending_session_history_navigation->web_content_restore_mode;
    m_session_history = move(m_pending_session_history_navigation->previous_session_history);
    m_pending_session_history_navigation.clear();
    m_pending_session_history_traversal.clear();

    RestorePendingSessionHistoryNavigationResult result {
        .restored = true,
        .web_content_restore_mode = web_content_restore_mode,
    };

    if (auto* current_entry = m_session_history.current_entry()) {
        result.current_url = current_entry->url;
        if (web_content_restore_mode == PendingSessionHistoryNavigation::WebContentRestoreMode::PreserveCurrentProcessState) {
            m_session_history_entry_url_loading_from_ui_process.clear();
            abandon_pending_web_content_session_history_seed();
            m_current_web_content_session_history_matches_mirror = m_session_history.web_content_history_matches_mirror();
            result.current_web_content_session_history_matches_mirror = m_current_web_content_session_history_matches_mirror;
        }
    } else {
        m_current_web_content_session_history_matches_mirror = false;
    }

    return result;
}

HistoryTraversalDecision CanonicalTraversable::traverse_the_history_by_delta(int delta, CheckForCancelation check_for_cancelation, URL::URL const& current_url, Function<void(HistoryTraversalOutcome)> on_cancelation_check_complete)
{
    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#traverse-the-history-by-a-delta
    // Let allSteps be the result of getting all used history steps for traversable.
    // Let currentStepIndex be the index of traversable's current session history step within allSteps.
    // Let targetStepIndex be currentStepIndex plus delta.
    //
    // AD-HOC: Browser UI calls do not pass sourceDocument. Since the UI process owns the top-level session history
    //         mirror, it resolves targetStepIndex before asking WebContent to apply or precheck the resulting
    //         traverse history step.
    auto target = m_session_history.traversal_target_for_delta(delta);
    if (!target.has_value())
        return {
            .outcome = { .status = HistoryTraversalStatus::NoEntry },
        };

    auto will_replace_web_content_process = !is_url_suitable_for_same_process_navigation(current_url, target->target_top_level_entry->url);
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
        && !m_session_history_entry_url_loading_from_ui_process.has_value()
        && !m_pending_web_content_session_history_seed.step_after_loading_top_level_entry.has_value()
        && m_session_history.web_content_can_traverse_to(*target);

    // If WebContent already has enough state to apply the traverse history step,
    // let it run the spec algorithm directly.
    if (web_content_can_apply_traversal && !will_replace_web_content_process) {
        m_pending_session_history_traversal = move(pending_traversal);
        auto webdriver_pending_navigation_completes_with_session_history_update = false;
        if (auto const* current_entry = m_session_history.current_entry()) {
            webdriver_pending_navigation_completes_with_session_history_update = current_entry->document_state.id != 0
                && current_entry->document_state.id == target->target_top_level_entry->document_state.id;
        }
        return {
            .outcome = {
                .status = HistoryTraversalStatus::Started,
                .will_replace_web_content_process = will_replace_web_content_process,
                .will_change_top_level_entry = target->changes_top_level_entry,
            },
            .action = HistoryTraversalAction::TraverseInWebContent,
            .target_step = target->target_step,
            .webdriver_pending_navigation_url = target->target_top_level_entry->url,
            .webdriver_pending_navigation_completes_with_session_history_update = webdriver_pending_navigation_completes_with_session_history_update,
        };
    }

    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#apply-the-history-step
    // If checkForCancelation is true, and the result of checking if unloading is canceled given
    // navigablesCrossingDocuments, traversable, targetStep, and userInvolvement is not "continue", then return that
    // result.
    //
    // AD-HOC: If WebContent cannot apply the step itself, the UI still asks it to run that precheck before moving
    //         the UI-owned history mirror and loading or reseeding WebContent from that state. A renderer-initiated
    //         traversal can report that this already happened, but only trust that precheck if WebContent can
    //         traverse to the same target step in the UI-owned history mirror.
    auto needs_cancelation_check = check_for_cancelation == CheckForCancelation::Yes
        || (check_for_cancelation == CheckForCancelation::IfWebContentCannotTraverseTarget && !web_content_can_apply_traversal);
    if (needs_cancelation_check) {
        pending_traversal.stage = PendingSessionHistoryTraversal::Stage::CheckingCancelation;
        pending_traversal.cancelation_check_request_id = m_next_traverse_history_step_cancelation_check_request_id++;
        pending_traversal.on_cancelation_check_complete = move(on_cancelation_check_complete);
        auto request_id = pending_traversal.cancelation_check_request_id;
        m_pending_session_history_traversal = move(pending_traversal);
        return {
            .outcome = {
                .status = HistoryTraversalStatus::Started,
                .will_replace_web_content_process = will_replace_web_content_process,
                .will_change_top_level_entry = target->changes_top_level_entry,
                .waiting_for_cancelation_check = true,
            },
            .action = HistoryTraversalAction::CheckForCancelation,
            .target_step = target->target_step,
            .cancelation_check_request_id = request_id,
        };
    }

    pending_traversal.stage = PendingSessionHistoryTraversal::Stage::LoadingEntryFromUIProcess;
    m_pending_session_history_traversal = move(pending_traversal);
    prepare_to_load_session_history_traversal_target_from_ui_process(*target, current_url);
    return {
        .outcome = {
            .status = HistoryTraversalStatus::Started,
            .will_replace_web_content_process = will_replace_web_content_process,
            .will_change_top_level_entry = target->changes_top_level_entry,
        },
        .action = HistoryTraversalAction::LoadCurrentEntryFromUIProcess,
        .webdriver_pending_navigation_url = target->target_top_level_entry->url,
        .webdriver_pending_navigation_completes_with_session_history_update = true,
    };
}

URL::URL CanonicalTraversable::prepare_to_load_session_history_traversal_target_from_ui_process(TraversableSessionHistory::TraversalTarget const& target, URL::URL const& current_url)
{
    if (!m_pending_session_history_traversal.has_value() || m_pending_session_history_traversal->target_step != target.target_step) {
        m_pending_session_history_traversal = PendingSessionHistoryTraversal {
            .target_step = target.target_step,
            .target_step_index = target.target_step_index,
            .will_change_top_level_entry = target.changes_top_level_entry,
            .will_replace_web_content_process = !is_url_suitable_for_same_process_navigation(current_url, target.target_top_level_entry->url),
            .stage = PendingSessionHistoryTraversal::Stage::LoadingEntryFromUIProcess,
            .on_cancelation_check_complete = nullptr,
        };
    } else {
        m_pending_session_history_traversal->stage = PendingSessionHistoryTraversal::Stage::LoadingEntryFromUIProcess;
    }

    auto target_url = target.target_top_level_entry->url;
    auto previous_session_history = m_session_history;
    m_session_history.traverse_to(target.target_step_index);
    prepare_to_seed_web_content_session_history_from_ui_process();
    m_pending_session_history_navigation = PendingSessionHistoryNavigation {
        target_url,
        move(previous_session_history),
    };
    return target_url;
}

bool CanonicalTraversable::should_allow_current_entry_reconstruction_when_seeding(bool allow_current_entry_reconstruction) const
{
    // NB: A fallback traversal or crash recovery can restore the current top-level document before WebContent has the
    //     UI-owned session history that surrounds it. Allow WebContent to reconstruct that current entry only while the
    //     UI process is actively restoring that authoritative history, including the follow-up child navigable step for
    //     nested history. After crash recovery there might not be a pending traversal object anymore, but a pending
    //     nested step still means the top-level document is only a staging point for restoring the current history
    //     step.
    auto is_restoring_traversal_target = m_pending_session_history_traversal.has_value()
        && (m_pending_session_history_traversal->stage == PendingSessionHistoryTraversal::Stage::LoadingEntryFromUIProcess
            || m_pending_session_history_traversal->stage == PendingSessionHistoryTraversal::Stage::ReplacingWebContentProcess
            || m_pending_session_history_traversal->stage == PendingSessionHistoryTraversal::Stage::RestoringNestedStepAfterSeed);
    return is_restoring_traversal_target
        || m_pending_web_content_session_history_seed.step_after_loading_top_level_entry.has_value()
        || allow_current_entry_reconstruction;
}

void CanonicalTraversable::reset_session_history_for_testing()
{
    m_session_history.clear();
    m_current_web_content_session_history_matches_mirror = false;
    m_pending_session_history_navigation.clear();
    m_pending_session_history_traversal.clear();
    m_session_history_entry_url_loading_from_ui_process.clear();
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
    case PendingSessionHistoryTraversal::Stage::LoadingEntryFromUIProcess:
        return "loading-entry-from-ui-process"sv;
    case PendingSessionHistoryTraversal::Stage::ReplacingWebContentProcess:
        return "replacing-webcontent-process"sv;
    case PendingSessionHistoryTraversal::Stage::RestoringNestedStepAfterSeed:
        return "restoring-nested-step-after-seed"sv;
    }
    VERIFY_NOT_REACHED();
}

}
