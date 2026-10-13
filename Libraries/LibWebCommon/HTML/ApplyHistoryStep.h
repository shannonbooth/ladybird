/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Types.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#checking-if-unloading-is-canceled
enum class CheckIfUnloadingIsCanceledResult {
    CanceledByBeforeUnload,
    CanceledByNavigate,
    Continue,
};

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#apply-the-history-step
// They return "initiator-disallowed", "canceled-by-beforeunload", "canceled-by-navigate", or
// "applied".
enum class HistoryStepResult {
    InitiatorDisallowed,
    CanceledByBeforeUnload,
    CanceledByNavigate,
    Applied,
};

enum class UnloadPromptShown : bool {
    No,
    Yes,
};

enum class UnloadDisplayedDocument : bool {
    No,
    Yes,
};

enum class ChildNavigableDestruction : bool {
    No,
    Yes,
};

enum class StopHostingAfterUnload : bool {
    No,
    Yes,
};

struct HistoryObjectLengthAndIndex {
    u64 script_history_length;
    u64 script_history_index;
};

}
