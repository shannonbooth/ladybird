/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/HashTable.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/RefPtr.h>
#include <AK/Utf16String.h>
#include <AK/Vector.h>
#include <AK/WeakPtr.h>
#include <LibCore/Promise.h>
#include <LibWebCommon/Bindings/NavigationType.h>
#include <LibWebCommon/HTML/ApplyHistoryStep.h>
#include <LibWebCommon/HTML/CrossProcessId.h>
#include <LibWebCommon/HTML/HistoryOperation.h>
#include <LibWebCommon/HTML/NavigationSourceSnapshot.h>
#include <LibWebCommon/HTML/UserNavigationInvolvement.h>
#include <LibWebView/Export.h>
#include <LibWebView/Forward.h>
#include <LibWebView/SessionHistory.h>
#include <LibWebView/SessionHistoryTraversalQueue.h>

namespace WebView {

class ApplyHistoryStep;
struct ChangingNavigable;
struct NonchangingNavigable;

// What a run of apply the history step does in the processes hosting the documents. Each job reports to the run
// exactly once, including after its process is lost.
class WEBVIEW_API ApplyHistoryStepHost {
public:
    virtual ~ApplyHistoryStepHost() = default;

    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#checking-if-unloading-is-canceled
    // NB: Given targetStep's target entry, which the traversable's host resolves no further.
    virtual void check_if_unloading_is_canceled(ApplyHistoryStep&, Vector<Web::HTML::CrossProcessId> navigables_that_need_before_unload, NonnullRefPtr<CanonicalSessionHistoryEntry> target_entry, Web::HTML::UserNavigationInvolvement user_involvement_for_navigate_event, Function<void(Web::HTML::CheckIfUnloadingIsCanceledResult)> on_complete) = 0;

    // NB: This queued work runs in the process hosting the active Window.
    virtual void queue_navigation_api_state_clear_task(ChangingNavigable&) = 0;

    // One iteration of "12. For each navigable of changingNavigables, queue a global task ...", in the process hosting
    // the navigable. The job claims its navigable and either enqueues a changing navigable continuation in its own
    // process (Ready) or reports why it could not (see Web::HTML::ChangingNavigableHistoryStepJobDisposition). Returns
    // false if no process can run it.
    virtual bool run_changing_navigable_history_step_job(ChangingNavigable&) = 0;

    // The "second part" of a changing navigable's job ("12. In both cases, let afterPotentialUnloads be ..."),
    // applied to the continuation the job enqueued in its own process.
    virtual void apply_changing_navigable_history_step_continuation(ChangingNavigable&) = 0;

    // One iteration of "18. For each navigable of nonchangingNavigablesThatStillNeedUpdates, queue a global task ...".
    virtual void update_nonchanging_navigable_history_step_state(NonchangingNavigable&, Web::HTML::HistoryObjectLengthAndIndex) = 0;
};

// One of a run's changingNavigables: its history step job, and the changing navigable continuation the job enqueues.
// NB: The process running the job retains the rest of the continuation's state. What waits on the job holds a weak
//     pointer to the record, which goes with its run.
struct WEBVIEW_API ChangingNavigable : public Weakable<ChangingNavigable> {
    AK_ALLOC_WITH_KMALLOC;

    enum class JobState : u8 {
        NotRun,
        Running,
        ContinuationEnqueued,
        ApplyingContinuation,
        // Nothing more comes from the process that ran the job.
        Ended,
    };

    ChangingNavigable(ApplyHistoryStep& run, Web::HTML::CrossProcessId id, CanonicalNavigable* navigable);

    ApplyHistoryStep& run() const { return *m_run; }

    // The navigable, unless it is gone. Removing a navigable ends its job, so a pending job's navigable is there.
    CanonicalNavigable* navigable() const;

    // Ends the job, which will not activate the document it populated, and returns the state the job was in.
    [[nodiscard]] JobState end_job();

    bool has_pending_job() const { return first_is_one_of(job_state, JobState::Running, JobState::ContinuationEnqueued, JobState::ApplyingContinuation); }

    // The target entry selected from canonical session history when the job is dispatched. The process running the job
    // retains this exact entry instead of resolving a target step against its local session history projection. The
    // run looks it up again before it is activated, and follows a sync navigation that changed it (whatwg/html#12961).
    RefPtr<CanonicalSessionHistoryEntry> target_entry;
    // Whether targetEntry's document state's reload pending was true when the job claimed it. Populating the document
    // sets it to false.
    bool target_entry_reload_pending { false };
    // The target entry as the job claimed it, for the processes the job runs in.
    Web::HTML::SessionHistoryEntryDescriptor target_entry_descriptor() const;

    Web::HTML::CrossProcessId id;
    JobState job_state { JobState::NotRun };
    bool has_ongoing_history_traversal { false };
    // AD-HOC: What a same-document traversal yields to, with the admitted navigation it canceled.
    Web::HTML::TraversalYieldsTo traversal_yields_to { Web::HTML::TraversalYieldsTo::Nothing };
    Optional<Utf16String> canceled_navigation_id {};
    // The run's part of the continuation, which the process running the job applies with the rest.
    struct Continuation {
        Web::HTML::HistoryObjectLengthAndIndex history_object_length_and_index;
        Vector<Web::HTML::SessionHistoryEntryDescriptor> entries_for_navigation_api;
    };
    Optional<Continuation> continuation;

    // NB: For the process running the job: its choice for the continuation's step 10, the response body it populates
    //     targetEntry's document from, and the document it populated.
    Web::HTML::UnloadDisplayedDocument unload_displayed_document { Web::HTML::UnloadDisplayedDocument::No };
    RefPtr<NavigationLoader> population_loader;
    RefPtr<CanonicalDocument> document;

private:
    ApplyHistoryStep* m_run;
    WeakPtr<CanonicalNavigable> m_navigable;
};

// One of a run's nonchangingNavigablesThatStillNeedUpdates.
struct WEBVIEW_API NonchangingNavigable {
    NonchangingNavigable(ApplyHistoryStep& run, Web::HTML::CrossProcessId id, CanonicalNavigable* navigable);

    ApplyHistoryStep& run() const { return *m_run; }

    // The navigable, unless it is gone.
    CanonicalNavigable* navigable() const;

    Web::HTML::CrossProcessId id;
    bool update_in_flight { false };

private:
    ApplyHistoryStep* m_run;
    WeakPtr<CanonicalNavigable> m_navigable;
};

// The apply-history-step state a traversable navigable shares across runs.
struct TraversableApplyHistoryStepState {
    // https://html.spec.whatwg.org/multipage/document-sequences.html#tn-running-nested-apply-history-step
    bool running_nested_apply_history_step { false };

    // AD-HOC: Concurrent runs share the current-step commit: a run commits its target step only if no newer run (a
    //         synchronous navigation that jumped the queue while it was paused) has moved the current step first, so
    //         the current step cannot move backwards past a newer run's commit. A newer run whose commit moves nothing
    //         (a synchronous replace re-commits the step it started from) leaves the paused run's own commit intact.
    //         See https://github.com/whatwg/html/issues/12576.
    u64 generation_counter { 0 };
    u64 committed_generation { 0 };
};

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#apply-the-history-step
//
// One run of the apply-the-history-step algorithm, coordinated from the UI process. The algorithm's traversal-wide
// variables live on this object; the per-navigable jobs run through caller-supplied functions in the processes
// hosting the documents, and their completion callbacks resume the algorithm at the phase that dispatched them.
class WEBVIEW_API ApplyHistoryStep
    : public Weakable<ApplyHistoryStep> {
    AK_MAKE_NONCOPYABLE(ApplyHistoryStep);
    AK_MAKE_NONMOVABLE(ApplyHistoryStep);

public:
    AK_ALLOC_WITH_KMALLOC;

    ApplyHistoryStep(
        TraversableSessionHistory& session_history,
        CanonicalNavigable& traversable_navigable,
        SessionHistoryTraversalQueue& session_history_traversal_queue,
        TraversableApplyHistoryStepState& traversable_state,
        ApplyHistoryStepHost& host,
        HistoryOperation* operation,
        Web::HTML::CrossProcessId operation_id,
        u64 operation_sequence_number,
        i32 step,
        bool check_for_cancelation,
        Optional<Web::HTML::CrossProcessId> initiator_to_check,
        Optional<Web::HTML::NavigationSourceSnapshot> source_snapshot_params,
        Web::HTML::UserNavigationInvolvement user_involvement,
        Optional<Web::Bindings::NavigationType> navigation_type,
        Function<void(Web::HTML::HistoryStepResult)> on_complete);
    ~ApplyHistoryStep();

    void apply_the_history_step();
    // The run's operation ended without it. The run calls none of its jobs from then on.
    void abandon();

    bool completed() const { return m_completed; }
    // The step this run committed as the current session history step, if it was the run that committed.
    Optional<i32> committed_step() const { return m_committed_step; }

    // The history operation the run belongs to, for the code dispatching its jobs.
    HistoryOperation& operation() const
    {
        VERIFY(m_operation);
        return *m_operation;
    }
    Web::HTML::CrossProcessId operation_id() const { return m_operation_id; }
    Web::HTML::UserNavigationInvolvement user_involvement() const { return m_user_involvement; }
    Optional<Web::Bindings::NavigationType> navigation_type() const { return m_navigation_type; }
    Optional<Web::HTML::NavigationSourceSnapshot> const& source_snapshot_params() const { return m_source_snapshot_params; }

    Vector<NonnullOwnPtr<ChangingNavigable>> const& changing_navigables() const { return m_changing_navigables; }
    ChangingNavigable* changing_navigable(Web::HTML::CrossProcessId);
    NonchangingNavigable* nonchanging_navigable(Web::HTML::CrossProcessId);

    // What the jobs report, each once.
    void changing_navigable_job_completed(ChangingNavigable&, Web::HTML::ChangingNavigableHistoryStepJobDisposition);
    void changing_navigable_continuation_applied(ChangingNavigable&);
    void nonchanging_navigable_updated(NonchangingNavigable&);
    // A job that ended in job_state counts as one that completed with disposition, or whose continuation was applied.
    // NB: A continuation already enqueued finds its job ended when it is dequeued.
    void changing_navigable_job_ended(ChangingNavigable&, ChangingNavigable::JobState, Web::HTML::ChangingNavigableHistoryStepJobDisposition);
    void end_changing_navigable_job(ChangingNavigable& changing_navigable, Web::HTML::ChangingNavigableHistoryStepJobDisposition disposition) { changing_navigable_job_ended(changing_navigable, changing_navigable.end_job(), disposition); }

private:
    void get_changing_and_nonchanging_navigables();
    void run_changing_navigable_jobs();
    void process_changing_navigable_continuations();
    void update_nonchanging_navigables();
    void set_current_session_history_step();
    CanonicalSessionHistoryEntry* append_the_claimed_target_entry_again(CanonicalNavigable&, CanonicalSessionHistoryEntry& claimed_target_entry);

    void set_ongoing_navigation_to_traversal(ChangingNavigable&, CanonicalNavigable&, CanonicalSessionHistoryEntry const&);
    void clear_ongoing_navigation_traversal(ChangingNavigable&);
    void clear_all_ongoing_navigation_traversals();
    void return_result(Web::HTML::HistoryStepResult);
    CanonicalNavigable* find_navigable(Web::HTML::CrossProcessId);

    TraversableSessionHistory& m_session_history;
    CanonicalNavigable& m_traversable_navigable;
    SessionHistoryTraversalQueue& m_session_history_traversal_queue;
    TraversableApplyHistoryStepState& m_traversable_state;
    ApplyHistoryStepHost& m_host;

    HistoryOperation* const m_operation;
    Web::HTML::CrossProcessId const m_operation_id;
    u64 const m_operation_sequence_number;

    // The algorithm's arguments.
    // NB: A run that appends its claimed target entry again moves to that entry's step.
    i32 m_step;
    // NB: The HTML Standard spells this algorithm argument "checkForCancelation".
    bool const m_check_for_cancelation;
    Optional<Web::HTML::CrossProcessId> const m_initiator_to_check;
    Optional<Web::HTML::NavigationSourceSnapshot> const m_source_snapshot_params;
    Web::HTML::UserNavigationInvolvement const m_user_involvement;
    Optional<Web::Bindings::NavigationType> const m_navigation_type;
    Function<void(Web::HTML::HistoryStepResult)> m_on_complete;

    // The algorithm's variables.
    i32 m_target_step { 0 };
    Vector<NonnullOwnPtr<ChangingNavigable>> m_changing_navigables;
    Vector<NonchangingNavigable> m_nonchanging_navigables_that_still_need_updates;
    size_t m_completed_change_jobs { 0 };
    Vector<ChangingNavigable*> m_changing_navigable_continuations;
    HashTable<Web::HTML::CrossProcessId> m_navigables_that_must_wait_before_handling_sync_navigation;
    size_t m_completed_nonchanging_jobs { 0 };

    // AD-HOC: The generation that orders the runs' commits of the current step (whatwg/html#12576).
    u64 m_generation;
    // How far the run lets synchronous navigation steps jump.
    Optional<u64> m_synchronous_navigation_steps_to_jump_through;

    // The synchronous navigation steps this run is paused on ("running nested apply history step" is true).
    RefPtr<Core::Promise<Empty>> m_running_synchronous_navigation_steps;

    bool m_changing_jobs_dispatched { false };
    bool m_processing_changing_navigable_continuations { false };
    bool m_continuation_in_flight { false };

    Optional<i32> m_committed_step;
    bool m_completed { false };
};

}
