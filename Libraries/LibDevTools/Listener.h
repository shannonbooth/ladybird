/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/OwnPtr.h>

namespace DevTools {

// An actor hears of what it listens for in a tab for as long as it holds what the delegate gave it.
class Listener {
public:
    virtual ~Listener() = default;
};

}
