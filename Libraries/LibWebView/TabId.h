/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/DistinctNumeric.h>

namespace WebView {

// Identifies a tab independently of its UI and the processes hosting its documents.
AK_TYPEDEF_DISTINCT_ORDERED_ID(u64, TabId);

}
