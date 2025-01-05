/*
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

namespace Web::URLPattern {

WebIDL::ExceptionOr<String> canonicalize_a_protocol(String const&);
String canonicalize_a_username(String const&);
String canonicalize_a_password(String const&);
WebIDL::ExceptionOr<String> canonicalize_a_hostname(String const&);
WebIDL::ExceptionOr<String> canonicalize_an_ipv6_hostname(String const&);
WebIDL::ExceptionOr<String> canonicalize_a_port(String const&, Optional<String> const& protocol_value = {});
WebIDL::ExceptionOr<String> canonicalize_a_pathname(String const&);
WebIDL::ExceptionOr<String> canonicalize_an_opaque_pathname(String const&);
WebIDL::ExceptionOr<String> canonicalize_a_search(String const&);
WebIDL::ExceptionOr<String> canonicalize_a_hash(String const&);

}
