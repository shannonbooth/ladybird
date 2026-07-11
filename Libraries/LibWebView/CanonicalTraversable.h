/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/HashFunctions.h>
#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <AK/Variant.h>
#include <AK/Vector.h>
#include <AK/WeakPtr.h>
#include <LibURL/URL.h>
#include <LibWeb/Bindings/Navigation.h>
#include <LibWeb/HTML/LocalNavigable.h>
#include <LibWeb/HTML/VisibilityState.h>
#include <LibWebView/CanonicalNavigable.h>
#include <LibWebView/Export.h>
#include <LibWebView/SessionHistory.h>

namespace WebView {

enum class HistoryTraversalStatus : u8 {
    Started,
    NoEntry,
    Canceled,
};

// NB: The HTML Standard spells this algorithm argument "checkForCancelation".
enum class CheckForCancelation : u8 {
    Yes,
    No,
    IfWebContentCannotTraverseTarget,
};

struct HistoryTraversalOutcome {
    HistoryTraversalStatus status { HistoryTraversalStatus::NoEntry };
    bool will_replace_web_content_process { false };
    bool will_change_top_level_entry { false };
    bool waiting_for_cancelation_check { false };
};

struct PendingSessionHistoryNavigation {
    enum class WebContentRestoreMode : u8 {
        PreserveCurrentProcessState,
        RestoreFromUIProcess,
    };

    URL::URL url;
    TraversableSessionHistory previous_session_history;
    WebContentRestoreMode web_content_restore_mode { WebContentRestoreMode::PreserveCurrentProcessState };
};

struct PendingWebContentSessionHistorySeed {
    bool should_send_entries { false };
    bool ignore_updates_until_seed { false };
    bool waiting_for_ack { false };
    // The history step WebContent must apply once it has accepted the seeded entries: a traversal
    // target when the UI process drives the traversal, or a nested step to restore after loading
    // the current top-level entry.
    Optional<i32> step_to_traverse_after_seed;

    // NB: Multi-field updates must go through the named transitions below. Each transition states
    //     which fields it preserves, so a transition that begins or supersedes a history operation
    //     cannot accidentally drop — or accidentally keep — another operation's seed state.

    // Forget any previous seed attempt entirely and arm a fresh send of the UI-owned entries.
    void reset_to_send_entries(Optional<i32> step_to_traverse)
    {
        clear();
        step_to_traverse_after_seed = step_to_traverse;
        should_send_entries = true;
        ignore_updates_until_seed = true;
    }

    // Arm another send of the UI-owned entries without forgetting the in-flight operation:
    // step_to_traverse_after_seed (a traversal that must still happen once the entries are
    // applied) survives, and any stale ack is no longer awaited.
    void resend_entries()
    {
        waiting_for_ack = false;
        should_send_entries = true;
        ignore_updates_until_seed = true;
    }

    // The entries went out; nothing further to send until WebContent acks this seed.
    void mark_entries_sent()
    {
        waiting_for_ack = true;
        should_send_entries = false;
    }

    void clear() { *this = {}; }
};

struct PendingSessionHistoryTraversal {
    enum class Stage : u8 {
        ApplyingInWebContent,
        CheckingCancelation,
        SeedingHistoryFromUIProcess,
        ReplacingWebContentProcess,
        ApplyingSeededHistoryStep,
    };

    i32 target_step { 0 };
    size_t target_step_index { 0 };
    u64 cancelation_check_request_id { 0 };
    bool will_change_top_level_entry { false };
    bool will_replace_web_content_process { false };
    Stage stage { Stage::ApplyingInWebContent };
    Function<void(HistoryTraversalOutcome)> on_cancelation_check_complete;
};

// NB: The results below tell ViewImplementation which UI-process side effects to apply. Each
//     carries the reason string for the session-history debug dump, so the producer is the
//     single place that decides both the state transition and how it is logged.

struct WebContentSessionHistoryUpdateResult {
    TraversableSessionHistory::UpdateResult update_result { TraversableSessionHistory::UpdateResult::InvalidSnapshot };
    Optional<URL::URL> current_url {};
    bool should_seed_web_content { false };
};

struct WebContentSessionHistoryUpdateDecision {
    // When set, the snapshot was ignored and the UI mirror was left untouched.
    Optional<StringView> ignore_reason {};
    WebContentSessionHistoryUpdateResult update {};
};

struct WebContentSessionHistorySeedAckResult {
    bool ignored { false };
    StringView dump_reason;
    Optional<URL::URL> current_url {};
    Optional<i32> step_to_traverse {};
    bool should_complete_webdriver_pending_navigation { false };
    bool should_update_navigation_action_state { false };
};

struct NavigationStartResult {
    Optional<StringView> dump_reason {};
    bool should_update_navigation_action_state { false };
    bool should_update_webdriver_pending_navigation_url { false };
    bool did_clear_crash_page { false };
};

enum class NavigationCancelStatus : u8 {
    Ignored,
    RestorePendingSessionHistoryNavigation,
    CanceledUIHistoryLoad,
    CompleteWebdriverPendingNavigation,
};

struct NavigationCancelResult {
    NavigationCancelStatus status { NavigationCancelStatus::Ignored };
};

struct NavigationFinishResult {
    bool should_seed_web_content { false };
    Optional<StringView> dump_reason {};
};

struct RestorePendingSessionHistoryNavigationResult {
    bool restored { false };
    Optional<URL::URL> current_url {};
    PendingSessionHistoryNavigation::WebContentRestoreMode web_content_restore_mode { PendingSessionHistoryNavigation::WebContentRestoreMode::PreserveCurrentProcessState };
};

enum class HistoryTraversalAction : u8 {
    None,
    TraverseInWebContent,
    CheckForCancelation,
    SeedHistoryAndTraverseInWebContent,
};

struct HistoryTraversalDecision {
    HistoryTraversalOutcome outcome;
    HistoryTraversalAction action { HistoryTraversalAction::None };
    Optional<i32> target_step {};
    Optional<u64> cancelation_check_request_id {};
    Optional<URL::URL> webdriver_pending_navigation_url {};
    bool webdriver_pending_navigation_completes_with_session_history_update { false };
};

struct WebContentHistoryStepResult {
    StringView dump_reason;
    // When set, the UI-owned target entry must be loaded from the UI process instead.
    Optional<TraversableSessionHistory::TraversalTarget> fallback_target {};
    bool should_restore_pending_navigation { false };
    bool should_update_navigation_action_state { false };
    Optional<URL::URL> current_url {};
    bool should_complete_webdriver_pending_navigation { false };
    bool should_update_webdriver_pending_navigation_to_current_url { false };
    bool should_reset_webdriver_pending_navigation_completion { false };
};

struct HistoryStepCancelationCheckResult {
    StringView dump_reason;
    Function<void(HistoryTraversalOutcome)> on_cancelation_check_complete {};
    HistoryTraversalOutcome outcome {};
    // When set, the UI-owned target entry must be loaded from the UI process after
    // reporting the outcome.
    Optional<TraversableSessionHistory::TraversalTarget> target {};
    bool should_update_navigation_action_state { false };
    bool should_complete_webdriver_pending_navigation { false };
    bool should_update_webdriver_pending_navigation_to_current_url { false };
    bool should_reset_webdriver_pending_navigation_completion { false };
};

struct WebContentSessionHistorySeed {
    Vector<Web::HTML::SessionHistoryEntryDescriptor> entries;
    size_t current_top_level_entry_index { 0 };
    // True when the seed installs entries that a traversal will populate, rather than entries
    // whose current entry corresponds to a document that is (or is about to start) loading.
    bool current_entry_will_be_populated_by_traversal { false };
};

struct CurrentSessionHistoryEntryLoad {
    URL::URL url;
    Variant<Empty, String, Web::HTML::POSTResource> document_resource;
    Web::Bindings::NavigationHistoryBehavior history_handling { Web::Bindings::NavigationHistoryBehavior::Auto };
};

struct ProcessSwapNavigationPreparation {
    bool should_update_navigation_action_state { false };
    bool should_seed_web_content_before_load { false };
};

struct PageLoadPreparation {
    bool should_defer_ui_process_history_update { false };
    bool should_update_navigation_action_state { false };
};

struct WebContentCrashRecoveryPreparation {
    bool should_restore_current_entry_from_ui_process { false };
};

class WEBVIEW_API CanonicalTraversable final
    : public CanonicalNavigable {
public:
    CanonicalTraversable();

    virtual bool is_top_level_traversable() const override { return true; }

    CanonicalNavigable& insert(WebContentClient& reporting_client, u64 page_id, Web::HTML::NavigableId parent_frame_id, Web::HTML::NavigableId frame_id, CanonicalNavigable& fallback_parent);
    Optional<CanonicalNavigable&> find(Web::HTML::NavigableId frame_id);
    Optional<CanonicalNavigable const&> find(Web::HTML::NavigableId frame_id) const;
    void remove(CanonicalNavigable&);

    TraversableSessionHistory const& session_history() const { return m_session_history; }

    Web::HTML::VisibilityState system_visibility_state() const { return m_system_visibility_state; }
    void set_system_visibility_state(Web::HTML::VisibilityState visibility_state) { m_system_visibility_state = visibility_state; }

    bool current_web_content_session_history_matches_mirror() const { return m_current_web_content_session_history_matches_mirror; }

    Optional<PendingSessionHistoryNavigation> const& pending_session_history_navigation() const { return m_pending_session_history_navigation; }
    Optional<PendingSessionHistoryTraversal> const& pending_session_history_traversal() const { return m_pending_session_history_traversal; }

    Optional<URL::URL> const& session_history_entry_url_loading_from_ui_process() const { return m_session_history_entry_url_loading_from_ui_process; }
    PendingWebContentSessionHistorySeed const& pending_web_content_session_history_seed() const { return m_pending_web_content_session_history_seed; }

    ProcessSwapNavigationPreparation prepare_for_process_swap_navigation(URL::URL const&, Variant<Empty, String, Web::HTML::POSTResource>, Web::Bindings::NavigationHistoryBehavior);
    PageLoadPreparation prepare_for_page_load(URL::URL const&, Web::Bindings::NavigationHistoryBehavior);
    void prepare_for_non_history_page_load();
    void prepare_for_reload();
    void prepare_to_seed_web_content_session_history_from_ui_process();
    void prepare_to_seed_web_content_session_history_from_ui_process_and_traverse_to_step(i32);
    WebContentSessionHistoryUpdateDecision did_receive_web_content_session_history_update(Vector<Web::HTML::SessionHistoryEntryDescriptor>, Vector<i32> used_steps, size_t current_used_step_index, URL::URL const& current_url);
    WebContentSessionHistoryUpdateDecision did_receive_web_content_session_history_update_for_testing(Vector<Web::HTML::SessionHistoryEntryDescriptor>, Vector<i32> used_steps, size_t current_used_step_index, URL::URL const& current_url);
    WebContentSessionHistorySeedAckResult did_receive_web_content_session_history_seed_ack(bool accepted, Vector<Web::HTML::SessionHistoryEntryDescriptor>, Vector<i32> used_steps, size_t current_used_step_index, URL::URL const& current_url);
    NavigationStartResult did_start_navigation(URL::URL const&, Variant<Empty, String, Web::HTML::POSTResource>, bool is_redirect, Web::Bindings::NavigationHistoryBehavior, bool is_showing_crash_page);
    NavigationCancelResult did_cancel_navigation(URL::URL const&, bool has_webdriver_pending_navigation);
    NavigationFinishResult did_finish_navigation(URL::URL const&);
    RestorePendingSessionHistoryNavigationResult restore_pending_session_history_navigation();
    HistoryTraversalDecision traverse_the_history_by_delta(int delta, CheckForCancelation, URL::URL const& current_url, Function<void(HistoryTraversalOutcome)> on_cancelation_check_complete);
    URL::URL prepare_to_seed_session_history_and_traverse_to_step_from_ui_process(TraversableSessionHistory::TraversalTarget const&, URL::URL const& current_url);
    WebContentHistoryStepResult did_traverse_the_history_to_step(i32 step, bool step_was_available, Web::HTML::HistoryStepResult);
    HistoryStepCancelationCheckResult did_check_if_traverse_history_step_is_canceled(u64 request_id, i32 step, bool canceled);
    Optional<WebContentSessionHistorySeed> prepare_web_content_session_history_seed();
    CurrentSessionHistoryEntryLoad prepare_current_session_history_entry_load(URL::URL const& current_url);
    void did_send_web_content_session_history_seed();
    WebContentCrashRecoveryPreparation prepare_for_web_content_crash_recovery();
    WebContentCrashRecoveryPreparation prepare_to_reload_from_crash_page();
    void reset_session_history_for_testing();
    void mark_web_content_session_history_stale_for_testing();

    static StringView pending_session_history_navigation_web_content_restore_mode_to_string(PendingSessionHistoryNavigation::WebContentRestoreMode);
    static StringView pending_session_history_traversal_stage_to_string(PendingSessionHistoryTraversal::Stage);

private:
    void abandon_pending_web_content_session_history_seed();
    WebContentCrashRecoveryPreparation prepare_to_seed_and_traverse_to_current_step();
    void prepare_to_seed_web_content_session_history_from_ui_process(Optional<i32> step_to_traverse_after_seed);
    void ensure_pending_session_history_traversal(TraversableSessionHistory::TraversalTarget const&, URL::URL const& current_url, PendingSessionHistoryTraversal::Stage);
    void remove_from_index(CanonicalNavigable&);
    WebContentSessionHistoryUpdateResult update_session_history_from_web_content(Vector<Web::HTML::SessionHistoryEntryDescriptor>, Vector<i32> used_steps, size_t current_used_step_index, bool pending_step_to_traverse_after_seed_was_applied, bool seed_web_content_on_invalid_snapshot, URL::URL const& current_url);
    WebContentSessionHistoryUpdateResult adopt_web_content_session_history_after_rejected_seed(Vector<Web::HTML::SessionHistoryEntryDescriptor>, Vector<i32> used_steps, size_t current_used_step_index, URL::URL const& current_url);

    HashMap<Web::HTML::NavigableId, WeakPtr<CanonicalNavigable>> m_navigable_index;
    TraversableSessionHistory m_session_history;
    Web::HTML::VisibilityState m_system_visibility_state { Web::HTML::VisibilityState::Hidden };
    bool m_current_web_content_session_history_matches_mirror { false };
    u64 m_next_traverse_history_step_cancelation_check_request_id { 0 };

    // NB: The four members below together describe the single in-flight top-level history
    //     operation: a navigation, a traversal, a UI-process history entry load, and/or the
    //     WebContent seed serving it. A transition that begins or supersedes an operation must
    //     decide the fate of all four; updating a subset leaves a superseded operation's state
    //     armed, and a later IPC reply for that state will be misattributed to the new operation.
    Optional<PendingSessionHistoryNavigation> m_pending_session_history_navigation;
    Optional<PendingSessionHistoryTraversal> m_pending_session_history_traversal;
    Optional<URL::URL> m_session_history_entry_url_loading_from_ui_process;
    PendingWebContentSessionHistorySeed m_pending_web_content_session_history_seed;
};

}
