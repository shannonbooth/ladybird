/*
 * Copyright (c) 2026, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGC/Function.h>

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

using OnApplyHistoryStepComplete = GC::Function<void(HistoryStepResult)>;

}
