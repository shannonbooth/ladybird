/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/Variant.h>
#include <AK/Vector.h>
#include <LibCore/ImmutableBytes.h>
#include <LibHTTP/Header.h>
#include <LibRequests/CacheState.h>
#include <LibRequests/NetworkError.h>
#include <LibRequests/RequestTimingInfo.h>
#include <LibURL/URL.h>
#include <LibWeb/Fetch/Engine/Ids.h>
#include <LibWeb/Fetch/Engine/NetworkInterception.h>
#include <LibWeb/Fetch/Infrastructure/FetchControllerToken.h>
#include <LibWeb/Fetch/Infrastructure/FetchTimingInfo.h>
#include <LibWebCommon/HTML/CrossProcessId.h>
#include <LibWebCommon/Fetch/Infrastructure/HTTP/RequestPriority.h>
#include <LibWebCommon/ReferrerPolicy/ReferrerPolicy.h>

namespace Web::Fetch::Engine::Event {

// What happened to one of a fetch's RequestServer requests, as DevTools reads it.
struct TransportActivity {
    struct RequestStarted {
        URL::URL url;
        ByteString method;
        Vector<HTTP::Header> headers;
        Core::ImmutableBytes body;
        Optional<String> initiator_type;
        ReferrerPolicy::ReferrerPolicy referrer_policy { ReferrerPolicy::ReferrerPolicy::EmptyString };
        bool is_navigation_request { false };
        Infrastructure::RequestPriority priority { Infrastructure::RequestPriority::Auto };
    };
    struct ResponseHeaders {
        u32 status_code { 0 };
        Optional<String> reason_phrase;
        Vector<HTTP::Header> headers;
        Requests::CacheState cache_state { Requests::CacheState::NotCached };
    };
    struct ResponseBodyBytes {
        Core::ImmutableBytes bytes;
    };
    struct RequestFinished {
        u64 total_size { 0 };
        Requests::RequestTimingInfo timing_info;
        Optional<Requests::NetworkError> network_error;
    };
    using Activity = Variant<RequestStarted, ResponseHeaders, ResponseBodyBytes, RequestFinished>;

    RequestId request_id;
    Activity activity;
};

struct ObservedRequest {
    URL::URL url;
    ByteString method;
    Vector<HTTP::Header> headers;
    Optional<u64> body_size;
    String destination;
    Optional<String> initiator_type;
    u8 redirect_count;
    Optional<HTML::CrossProcessId> navigable_id;
    Optional<double> time_origin;
};

struct ObservedResponse {
    Optional<URL::URL> url;
    u16 status;
    ByteString reason;
    Vector<HTTP::Header> headers;
    ByteString protocol;
    bool from_cache;
    u64 encoded_size;
    u64 decoded_size;
};

struct FetchNetworkActivity {
    struct PhaseReached {
        NetworkPhase phase;
        Optional<InterceptionPoint> interception;
    };
    struct RequestUpdated { };
    struct ResponseCompleted { };
    struct Failed {
        String reason;
    };
    using Activity = Variant<PhaseReached, RequestUpdated, ResponseCompleted, Failed>;

    Optional<Infrastructure::FetchControllerToken> preflight_controller;
    ObservedRequest request;
    NonnullRefPtr<Infrastructure::FetchTimingInfo const> timing;
    Optional<ObservedResponse> response;
    Activity activity;
};

using NetworkActivity = Variant<TransportActivity, FetchNetworkActivity>;

}
