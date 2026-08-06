/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Types.h>
#include <LibGC/Function.h>
#include <LibWeb/Bindings/NavigationType.h>
#include <LibWeb/Forward.h>
#include <LibWeb/HTML/LocalNavigable.h>

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

using OnChangingNavigableHistoryStepJobComplete = GC::Function<void(ChangingNavigableHistoryStepJobDisposition)>;

}
