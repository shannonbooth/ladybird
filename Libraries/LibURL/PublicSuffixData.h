/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Forward.h>
#include <AK/Optional.h>

namespace URL {

class PublicSuffixData {
public:
    static bool is_public_suffix(StringView host);
    static Optional<String> get_public_suffix(StringView host);
};

}
