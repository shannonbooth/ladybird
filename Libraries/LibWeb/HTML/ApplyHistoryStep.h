/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGC/Function.h>
#include <LibWebCommon/HTML/ApplyHistoryStep.h>

namespace Web::HTML {

using OnApplyHistoryStepComplete = GC::Function<void(HistoryStepResult)>;

using OnEnqueueChangingNavigableContinuation = GC::Function<void(bool aborted, UnloadDisplayedDocument)>;
using OnSkipChangingNavigableHistoryStepJob = GC::Function<void()>;

}
