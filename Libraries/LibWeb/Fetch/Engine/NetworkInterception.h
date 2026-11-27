/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/ByteString.h>
#include <AK/Optional.h>
#include <AK/Variant.h>
#include <AK/Vector.h>
#include <LibHTTP/Header.h>
#include <LibURL/URL.h>

namespace Web::Fetch::Engine {

enum class NetworkPhase : u8 {
    PrepareRequest,
    BeforeRequest,
    ResponseHeaders,
    Authentication,
};

struct InterceptionPoint {
    u64 sequence;
    NetworkPhase phase;
    u8 redirect_count;
    bool operator==(InterceptionPoint const&) const = default;
};

struct ContinueFetch { };
struct FailFetch { };
struct ConfigureRequest {
    bool bypass_cache { false };
    Vector<HTTP::Header> extra_headers;
};
struct ModifyRequest {
    Optional<URL::URL> url;
    Optional<ByteString> method;
    Optional<Vector<HTTP::Header>> headers;
    Optional<ByteString> cookie;
    Optional<ByteBuffer> body;
};
struct ModifyResponse {
    Optional<u64> status;
    Optional<ByteString> reason;
    Optional<Vector<HTTP::Header>> headers;
    Optional<Vector<HTTP::Header>> cookies;
};
struct ProvideResponse {
    ModifyResponse response;
    Optional<ByteBuffer> body;
};
struct Authenticate {
    ByteString authorization;
    ModifyResponse response;
};
struct CancelAuthentication { };

using NetworkDecision = Variant<ContinueFetch, FailFetch, ConfigureRequest, ModifyRequest, ModifyResponse, ProvideResponse, Authenticate, CancelAuthentication>;

}
