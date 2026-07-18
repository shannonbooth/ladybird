/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/HashFunctions.h>
#include <AK/HashMap.h>
#include <AK/HashTable.h>
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
#include <LibWeb/Page/Page.h>
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
    bool should_reseed_after_current_history_load { false };
    Optional<i32> step_after_loading_top_level_entry;

    void clear() { *this = {}; }
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
    bool allow_current_entry_reconstruction { false };
    bool should_update_webdriver_pending_navigation_url { false };
    Optional<StringView> dump_reason {};
};

struct RestorePendingSessionHistoryNavigationResult {
    bool restored { false };
    Optional<URL::URL> current_url {};
    PendingSessionHistoryNavigation::WebContentRestoreMode web_content_restore_mode { PendingSessionHistoryNavigation::WebContentRestoreMode::PreserveCurrentProcessState };
};

enum class HistoryTraversalAction : u8 {
    None,
    HistoryStepApplied,
    HistoryStepCanceled,
    RestorePendingNavigation,
    LoadCurrentEntryFromUIProcess,
};

struct HistoryTraversalDecision {
    HistoryTraversalOutcome outcome;
    HistoryTraversalAction action { HistoryTraversalAction::None };
    Optional<URL::URL> webdriver_pending_navigation_url {};
    bool webdriver_pending_navigation_completes_with_session_history_update { false };
};

struct WebContentSessionHistorySeed {
    Vector<Web::HTML::SessionHistoryEntryDescriptor> entries;
    size_t current_top_level_entry_index { 0 };
    bool allow_current_entry_reconstruction { false };
};

struct CurrentSessionHistoryEntryLoad {
    URL::URL url;
    Web::HTML::DocumentResource document_resource;
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

class WEBVIEW_API CanonicalTraversable final
    : public CanonicalNavigable {
public:
    CanonicalTraversable();

    using SameDocumentNavigationCommitResult = TraversableSessionHistory::SameDocumentNavigationCommitResult;

    struct SynchronousNavigationSteps {
        Web::HTML::CrossProcessId target_navigable_id;
        Utf16String expected_current_navigation_api_key;
        Web::HTML::SameDocumentNavigationEntry target_entry;
        Optional<Utf16String> entry_to_replace_navigation_api_key;
        Function<void(Optional<SameDocumentNavigationCommitResult>)> on_complete;
    };

    struct TraversalOperation {
        struct ByDelta {
            int delta { 0 };
        };
        struct ToStep {
            i32 step { 0 };
        };
        struct ByNavigationAPIKey {
            Web::HTML::CrossProcessId navigable_id;
            Utf16String key;
        };
        struct Reload {
            Function<void()> steps;
        };

        enum class Stage : u8 {
            ApplyingHistoryStep,
            CheckingCancelation,
            LoadingEntryFromUIProcess,
            ReplacingWebContentProcess,
            RestoringNestedStepAfterSeed,
        };

        struct Endpoint {
            RefPtr<WebContentClient> client;
            u64 page_id { 0 };
            Optional<u64> initiation_id;
        };

        struct ApplyHistoryStepOperation {
            struct ChangingNavigableJob {
                Web::HTML::CrossProcessId navigable_id;
                Web::HTML::SessionHistoryEntryDescriptor target_entry;
                RefPtr<WebContentClient> client;
                u64 page_id { 0 };
                bool completed { false };
            };

            Vector<ChangingNavigableJob> changing_navigable_jobs;
            Vector<Web::HTML::CrossProcessId> ready_continuations;
            Vector<Web::HTML::CrossProcessId> nonchanging_navigables_that_still_need_updates;
            size_t completed_changing_navigable_jobs { 0 };
            Optional<Web::HTML::CrossProcessId> continuation_in_flight;
            HashTable<Web::HTML::CrossProcessId> navigables_that_must_wait_before_handling_sync_navigation;
            Optional<i32> web_content_current_step;
        };

        Variant<ByDelta, ToStep, ByNavigationAPIKey, Reload> requested_traversal;
        CheckForCancelation check_for_cancelation { CheckForCancelation::Yes };
        Web::HTML::UserNavigationInvolvement user_involvement { Web::HTML::UserNavigationInvolvement::BrowserUI };
        URL::URL current_url;
        Function<void(HistoryTraversalDecision)> on_ready_to_start;

        u64 operation_id { 0 };
        i32 target_step { 0 };
        size_t target_step_index { 0 };
        bool will_change_top_level_entry { false };
        bool will_replace_web_content_process { false };
        bool can_apply_in_current_web_content { false };
        bool webdriver_pending_navigation_completes_with_session_history_update { false };
        Stage stage { Stage::ApplyingHistoryStep };
        Optional<Endpoint> initiating_endpoint;
        Optional<Endpoint> root_endpoint;
        Vector<Endpoint> participating_endpoints;
        Optional<ApplyHistoryStepOperation> apply_history_step;
        Function<void(HistoryTraversalOutcome)> on_cancelation_check_complete;
    };

    virtual bool is_top_level_traversable() const override { return true; }

    CanonicalNavigable& insert(WebContentClient& reporting_client, u64 page_id, Web::HTML::CrossProcessId parent_frame_id, Web::HTML::CrossProcessId frame_id, CanonicalNavigable& fallback_parent);
    Optional<CanonicalNavigable&> find(Web::HTML::CrossProcessId navigable_id);
    Optional<CanonicalNavigable const&> find(Web::HTML::CrossProcessId navigable_id) const;
    void remove(CanonicalNavigable&);

    TraversableSessionHistory const& session_history() const { return m_session_history; }

    Web::HTML::VisibilityState system_visibility_state() const { return m_system_visibility_state; }
    void set_system_visibility_state(Web::HTML::VisibilityState visibility_state) { m_system_visibility_state = visibility_state; }

    bool current_web_content_session_history_matches_mirror() const { return m_current_web_content_session_history_matches_mirror; }

    Optional<PendingSessionHistoryNavigation> const& pending_session_history_navigation() const { return m_pending_session_history_navigation; }
    Optional<TraversalOperation> const& pending_session_history_traversal() const { return m_running_traversal_operation; }

    Optional<URL::URL> const& session_history_entry_url_loading_from_ui_process() const { return m_session_history_entry_url_loading_from_ui_process; }
    PendingWebContentSessionHistorySeed const& pending_web_content_session_history_seed() const { return m_pending_web_content_session_history_seed; }

    ProcessSwapNavigationPreparation prepare_for_process_swap_navigation(URL::URL const&, Web::HTML::DocumentResource, Web::Bindings::NavigationHistoryBehavior);
    PageLoadPreparation prepare_for_page_load(URL::URL const&, Web::Bindings::NavigationHistoryBehavior);
    void prepare_for_non_history_page_load();
    void prepare_for_reload();
    void prepare_to_seed_web_content_session_history_from_ui_process();
    WebContentSessionHistoryUpdateDecision did_receive_web_content_session_history_update_for_testing(Vector<Web::HTML::SessionHistoryEntryDescriptor>, Vector<i32> used_steps, size_t current_used_step_index, URL::URL const& current_url);
    bool did_create_top_level_traversable(Web::HTML::SessionHistoryEntryDescriptor initial_history_entry);
    bool update_session_history_entry_navigation_api_state(CanonicalNavigable const&, Utf16String const& navigation_api_key, Web::HTML::StorageSerializationRecord navigation_api_state);
    bool update_session_history_entry_scroll_restoration_mode(CanonicalNavigable const&, Utf16String const& navigation_api_key, Web::HTML::ScrollRestorationMode scroll_restoration_mode);
    bool update_session_history_entry_scroll_position_data(CanonicalNavigable const&, Utf16String const& navigation_api_key, Web::HTML::SessionHistoryEntryScrollPositionData scroll_position_data);
    bool update_session_history_entry_document_state_navigable_target_name(CanonicalNavigable const&, Utf16String const& navigation_api_key, Utf16String navigable_target_name);
    bool set_session_history_entry_document_state_reload_pending(CanonicalNavigable const&, Utf16String const& navigation_api_key, bool reload_pending);
    bool append_nested_history(CanonicalNavigable const& parent_navigable, Web::HTML::SessionHistoryNestedHistoryDescriptor);
    bool remove_nested_history(CanonicalNavigable const& parent_navigable, Web::HTML::CrossProcessId child_navigable_id);
    void request_to_finalize_same_document_navigation(CanonicalNavigable const&, Utf16String expected_current_navigation_api_key, Web::HTML::SameDocumentNavigationEntry target_entry, Optional<Utf16String> entry_to_replace_navigation_api_key, Function<void(Optional<SameDocumentNavigationCommitResult>)> on_complete);
    bool finalize_cross_document_navigation(CanonicalNavigable const&, Web::HTML::SessionHistoryEntryDescriptor history_entry, Optional<Utf16String> entry_to_replace_navigation_api_key);
    Optional<i32> navigation_api_traversal_target(CanonicalNavigable const&, Utf16String const& navigation_api_key) const;
    WebContentSessionHistorySeedAckResult did_receive_web_content_session_history_seed_ack(bool accepted, Vector<Web::HTML::SessionHistoryEntryDescriptor>, Vector<i32> used_steps, size_t current_used_step_index, URL::URL const& current_url);
    NavigationStartResult did_start_navigation(URL::URL const&, Web::HTML::DocumentResource, bool is_redirect, Web::Bindings::NavigationHistoryBehavior, bool is_showing_crash_page);
    NavigationCancelResult did_cancel_navigation(URL::URL const&, bool has_webdriver_pending_navigation);
    NavigationFinishResult did_finish_navigation(URL::URL const&);
    RestorePendingSessionHistoryNavigationResult restore_pending_session_history_navigation();
    void append_reload_history_step(Function<void()> steps);
    void traverse_the_history_by_delta(int delta, CheckForCancelation, URL::URL const& current_url, Function<void(HistoryTraversalOutcome)> on_cancelation_check_complete, Function<void(HistoryTraversalDecision)> on_ready_to_start, Optional<TraversalOperation::Endpoint> initiating_endpoint = { }, Web::HTML::UserNavigationInvolvement = Web::HTML::UserNavigationInvolvement::BrowserUI);
    void traverse_the_history_by_navigation_api_key(Web::HTML::CrossProcessId navigable_id, Utf16String key, URL::URL const& current_url, Function<void(HistoryTraversalDecision)> on_ready_to_start, TraversalOperation::Endpoint initiating_endpoint);
    void traverse_the_history_to_step(i32 step, CheckForCancelation, URL::URL const& current_url, Function<void(HistoryTraversalOutcome)> on_cancelation_check_complete, Function<void(HistoryTraversalDecision)> on_ready_to_start, Optional<TraversalOperation::Endpoint> initiating_endpoint = { }, Web::HTML::UserNavigationInvolvement = Web::HTML::UserNavigationInvolvement::BrowserUI);
    URL::URL prepare_to_load_session_history_traversal_target_from_ui_process(TraversableSessionHistory::TraversalTarget const&, URL::URL const& current_url);
    void did_receive_history_step_cancelation_job_result(WebContentClient&, u64 page_id, u64 operation_id, Web::HTML::HistoryStepResult);
    void did_receive_changing_navigable_history_job_ready(WebContentClient&, u64 page_id, u64 operation_id, Web::HTML::CrossProcessId navigable_id, Web::ChangingNavigableHistoryStepJobDisposition);
    void did_apply_changing_navigable_continuation(WebContentClient&, u64 page_id, u64 operation_id, Web::HTML::CrossProcessId navigable_id);
    void continue_history_operation_after_web_content_seed(WebContentClient&, u64 page_id, i32 step);
    Optional<WebContentSessionHistorySeed> prepare_web_content_session_history_seed(bool allow_current_entry_reconstruction);
    CurrentSessionHistoryEntryLoad prepare_current_session_history_entry_load(URL::URL const& current_url);
    void did_send_web_content_session_history_seed();
    bool prepare_to_restore_current_session_history_entry_from_ui_process();
    void did_crash_requiring_web_content_session_history_seed();
    void reset_session_history_for_testing();
    void mark_web_content_session_history_stale_for_testing();

    static StringView pending_session_history_navigation_web_content_restore_mode_to_string(PendingSessionHistoryNavigation::WebContentRestoreMode);
    static StringView pending_session_history_traversal_stage_to_string(TraversalOperation::Stage);

private:
    using SessionHistoryTraversalQueueEntry = Variant<SynchronousNavigationSteps, TraversalOperation>;
    void append_session_history_traversal_operation(TraversalOperation);
    void run_session_history_traversal_queue();
    void schedule_session_history_traversal_queue();
    void start_running_traversal_operation();
    HistoryTraversalDecision traverse_the_history(TraversableSessionHistory::TraversalTarget const&);
    void dispatch_history_step_cancelation_job();
    void dispatch_changing_navigable_history_jobs();
    void process_changing_navigable_continuations();
    void finish_applying_history_step();
    void complete_running_traversal_operation(Web::HTML::HistoryStepResult = Web::HTML::HistoryStepResult::Applied, Optional<i32> committed_step = { });
    void run_synchronous_navigation_steps(SynchronousNavigationSteps);
    bool run_first_queued_synchronous_navigation_steps_not_targeting(HashTable<Web::HTML::CrossProcessId> const& excluded_navigables);
    void abandon_pending_web_content_session_history_seed();
    void reconcile_child_navigable_ids_after_session_history_reconstruction();
    void remove_from_index(CanonicalNavigable&);
    WebContentSessionHistoryUpdateResult update_session_history_from_web_content(Vector<Web::HTML::SessionHistoryEntryDescriptor>, Vector<i32> used_steps, size_t current_used_step_index, bool pending_step_after_fallback_load_was_restored, bool seed_web_content_on_invalid_snapshot, URL::URL const& current_url);
    WebContentSessionHistoryUpdateResult adopt_web_content_session_history_after_rejected_seed(Vector<Web::HTML::SessionHistoryEntryDescriptor>, Vector<i32> used_steps, size_t current_used_step_index, URL::URL const& current_url);

    HashMap<Web::HTML::CrossProcessId, WeakPtr<CanonicalNavigable>> m_navigable_index;
    TraversableSessionHistory m_session_history;
    Web::HTML::VisibilityState m_system_visibility_state { Web::HTML::VisibilityState::Hidden };
    bool m_current_web_content_session_history_matches_mirror { false };
    Optional<PendingSessionHistoryNavigation> m_pending_session_history_navigation;
    Vector<SessionHistoryTraversalQueueEntry> m_session_history_traversal_queue;
    Optional<TraversalOperation> m_running_traversal_operation;
    bool m_is_running_session_history_traversal_queue { false };
    bool m_session_history_traversal_queue_run_scheduled { false };
    u64 m_next_history_operation_id { 1 };
    Optional<URL::URL> m_session_history_entry_url_loading_from_ui_process;
    PendingWebContentSessionHistorySeed m_pending_web_content_session_history_seed;
};

}
