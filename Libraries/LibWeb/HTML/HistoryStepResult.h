/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/Vector.h>
#include <LibGC/Function.h>
#include <LibWeb/HTML/CrossProcessId.h>
#include <LibWeb/HTML/SessionHistoryEntry.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#apply-the-history-step
// They return "initiator-disallowed", "canceled-by-beforeunload", "canceled-by-navigate", or
// "applied".
enum class HistoryStepResult {
    InitiatorDisallowed,
    CanceledByBeforeUnload,
    CanceledByNavigate,
    // AD-HOC: This is an internal result used when WebContent no longer has the requested page.
    CanceledByMissingPage,
    // INTEROP: This is an internal result for browser UI handling and is not one of the results
    //          returned by the HTML Standard's apply the history step algorithm.
    CanceledPendingNavigation,
    Applied,
};

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#getting-the-history-object-length-and-index
struct HistoryObjectLengthAndIndex {
    u64 script_history_length;
    u64 script_history_index;
};

struct ChangingNavigableHistoryStepApplicationData {
    HistoryObjectLengthAndIndex history_object_length_and_index;
    Vector<SessionHistoryEntryDescriptor> entries_for_navigation_api;
};

using OnApplyHistoryStepComplete = GC::Function<void(HistoryStepResult)>;
using ResumeApplyingHistoryStepToChangingNavigable = GC::Function<void(Optional<ChangingNavigableHistoryStepApplicationData>)>;
using OnApplyHistoryStepReadyToApplyToChangingNavigable = GC::Function<void(CrossProcessId, GC::Ref<ResumeApplyingHistoryStepToChangingNavigable>)>;
using ResumeApplyingHistoryStep = GC::Function<void(Optional<HistoryObjectLengthAndIndex>)>;
using OnApplyHistoryStepChangingNavigablesComplete = GC::Function<void(GC::Ref<ResumeApplyingHistoryStep>)>;

// The document-local jobs from apply the history step have completed, but the traversable-wide
// current session history step has not yet been committed. The final callback completes the
// WebContent side after the canonical history owner has performed that commit.
//
// An "applied" result can also finish stale reconciliation work without changing the current
// session history step. Report that separately so the canonical history owner does not infer a
// current-step update from the algorithm's result.
using OnApplyHistoryStepJobsComplete = GC::Function<void(bool step_was_available, bool should_update_current_session_history_step, HistoryStepResult, GC::Ref<OnApplyHistoryStepComplete> finish_applying)>;

}
