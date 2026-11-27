/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Fetch/Engine/NetworkActivity.h>
#include <LibWeb/Fetch/Infrastructure/FetchParams.h>

namespace Web::Fetch::Fetching {

using Engine::Event::FetchNetworkActivity;

Engine::Event::ObservedRequest snapshot_request(Infrastructure::Request const&);
Engine::Event::ObservedResponse snapshot_response(Infrastructure::Response const&);
inline Optional<Infrastructure::FetchControllerToken> preflight_controller(Infrastructure::FetchParams const& params)
{
    if (params.is_cors_preflight())
        return params.controller_token();
    return {};
}
FetchNetworkActivity snapshot_network_activity(Optional<Infrastructure::FetchControllerToken>, Infrastructure::Request const&, Infrastructure::FetchTimingInfo const&, Infrastructure::Response const*, FetchNetworkActivity::Activity);

bool observes_network_activity(Infrastructure::FetchParams const&);
void report_network_activity(Infrastructure::FetchParams const&, Infrastructure::Response const*, FetchNetworkActivity::Activity);
bool should_intercept_network_phase(Infrastructure::FetchParams const&, Engine::NetworkPhase, Infrastructure::Response const* = nullptr);
enum class NetworkResult { Continue, RequestModified, ProvideResponse, Fail, ProvideCredentials, CancelAuthentication };
using ResumeNetworkFetch = Function<void(NetworkResult, RefPtr<Infrastructure::Response>)>;
void reach_network_phase(Infrastructure::FetchParams const&, Engine::NetworkPhase, RefPtr<Infrastructure::Response>, RefPtr<Infrastructure::Request> original_request, ResumeNetworkFetch);
void apply_request_header_overrides(Infrastructure::FetchParams const&);
void remove_content_length_override(Infrastructure::FetchParams const&);

}
