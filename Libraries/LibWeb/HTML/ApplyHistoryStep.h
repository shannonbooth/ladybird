/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/Vector.h>
#include <LibGC/Function.h>
#include <LibGC/Ptr.h>
#include <LibWeb/Bindings/NavigationType.h>
#include <LibWeb/Forward.h>
#include <LibWeb/HTML/CrossProcessId.h>
#include <LibWeb/HTML/LocalNavigable.h>
#include <LibWeb/HTML/SessionHistoryEntry.h>
#include <LibWeb/HTML/UserNavigationInvolvement.h>

namespace Web::HTML {

enum class SynchronousNavigation : bool {
    Yes,
    No,
};

enum class ChangingNavigableHistoryStepJobDisposition : u8 {
    Ready,
    Skipped,
    Stale,
};

struct HistoryObjectLengthAndIndex {
    u64 script_history_length;
    u64 script_history_index;
};

enum class InitiatorSandboxingCheckResult : u8 {
    Allowed,
    Disallowed,
};

using OnInitiatorSandboxingCheckComplete = GC::Function<void(InitiatorSandboxingCheckResult)>;
using OnHistoryStepUnloadCancelationComplete = GC::Function<void(HistoryStepResult)>;

// One iteration of "12. For each navigable of changingNavigables, queue a global task ...".
//
// The job claims its navigable before queueing its document work. Its continuation remains in WebContent under the
// operation that dispatched the job; only the disposition crosses the operation boundary.
struct ChangingNavigableHistoryStepJob {
    CrossProcessId navigable_id;
    int target_step { 0 };
    GC::Ptr<SourceSnapshotParams> source_snapshot_params;
    UserNavigationInvolvement user_involvement;
    Optional<Bindings::NavigationType> navigation_type;
    SynchronousNavigation synchronous_navigation;
    LocalNavigable::NavigationAPIAbortBehavior navigation_api_abort_behavior;
    GC::Ptr<DOM::Document> pending_document;
};

using OnChangingNavigableHistoryStepJobComplete = GC::Function<void(ChangingNavigableHistoryStepJobDisposition)>;

// The "second part" of a changing navigable's job: "12. In both cases, let afterPotentialUnloads be ...". The
// navigable identifies the continuation parked by the first part within the same operation.
struct ApplyChangingNavigableHistoryStepContinuation {
    CrossProcessId navigable_id;
    HistoryObjectLengthAndIndex history_object_length_and_index;
    Vector<SessionHistoryEntryDescriptor> entries_for_navigation_api;
    Optional<Bindings::NavigationType> navigation_type;
    LocalNavigable::NavigationAPIAbortBehavior navigation_api_abort_behavior;
    UserNavigationInvolvement user_involvement;
};

}
