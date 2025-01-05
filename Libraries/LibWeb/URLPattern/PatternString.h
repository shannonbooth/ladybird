/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <LibWeb/URLPattern/PatternParser.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

namespace Web::URLPattern {

String generate_a_pattern_string(Vector<Part> const&, PatternParser::Options const&);
String escape_a_pattern_string(String const&);

}
