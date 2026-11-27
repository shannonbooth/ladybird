/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Fetch/Engine/FetchEngineRecord.h>
#include <LibWeb/Fetch/Fetching/NetworkActivity.h>

namespace Web::Fetch::Fetching {

using namespace Engine;
using namespace Infrastructure;

Event::ObservedRequest snapshot_request(Request const& request)
{
    Optional<u64> length;
    if (auto const* bytes = request.body().get_pointer<ByteBuffer>())
        length = bytes->size();
    else if (auto const* body = request.body().get_pointer<FetchBody>())
        length = body->length();
    auto const& client = request.client_snapshot();
    Optional<HTML::CrossProcessId> navigable;
    if (client)
        navigable = client->navigable_id;
    if (!navigable.has_value()) {
        if (auto* id = request.traversable_for_user_prompts().get_pointer<HTML::CrossProcessId>())
            navigable = *id;
    }
    return {
        request.current_url(), request.method(), request.header_list()->headers(), length,
        request.destination().has_value() ? MUST(String::from_utf8(request_destination_to_string(*request.destination()))) : String {},
        request.initiator_type().map([](auto type) { return initiator_type_to_string(type).to_utf16_string().to_utf8(); }),
        request.redirect_count(), navigable, client ? Optional<double>(client->time_origin) : Optional<double> {}
    };
}

Event::ObservedResponse snapshot_response(Response const& response)
{
    auto url = response.url();
    auto info = response.body_info();
    if (auto const& body = response.body(); body.has_value() && body->source().has_value() && !info.decoded_size) {
        info.decoded_size = body->source()->size();
        if (url.has_value() && url->scheme() == "data"sv)
            info.encoded_size = info.decoded_size;
    }
    ByteString protocol;
    if (auto const& transport = response.network_transport(); transport.has_value())
        protocol = transport->handle->http_version();
    return { url.map([](auto const& value) { return value; }), response.status(), response.status_message(), response.header_list()->headers(), move(protocol),
        response.cache_state() == Response::CacheState::Local, info.encoded_size, info.decoded_size };
}

FetchNetworkActivity snapshot_network_activity(Optional<FetchControllerToken> controller, Request const& request, FetchTimingInfo const& timing, Response const* response, FetchNetworkActivity::Activity activity)
{
    auto snapshot = FetchTimingInfo::create(timing.seed());
    if (timing.final_connection_timing_info().has_value())
        snapshot->set_final_connection_timing_info(*timing.final_connection_timing_info());
    snapshot->set_final_network_request_start_time(timing.final_network_request_start_time());
    snapshot->set_final_network_response_start_time(timing.final_network_response_start_time());
    return { controller, snapshot_request(request), move(snapshot), response ? Optional<Event::ObservedResponse> { snapshot_response(*response) } : Optional<Event::ObservedResponse> {}, move(activity) };
}

bool observes_network_activity(FetchParams const& params)
{
    return params.engine_record() && params.engine_record()->network_activity_observers().observes_fetches();
}

void report_network_activity(FetchParams const& params, Response const* response, FetchNetworkActivity::Activity activity)
{
    if (observes_network_activity(params))
        params.engine_record()->report_network_activity(snapshot_network_activity(preflight_controller(params), *params.request(), *params.timing_info(), response, move(activity)));
}

bool should_intercept_network_phase(FetchParams const& params, NetworkPhase phase, Response const* response)
{
    auto* record = params.engine_record();
    if (!record)
        return false;
    if (record->network_activity_observers().can_intercept(phase))
        return true;
    if (phase != NetworkPhase::PrepareRequest)
        report_network_activity(params, response, FetchNetworkActivity::PhaseReached { phase, {} });
    return false;
}

static constexpr Array generated_headers {
    "Content-Length"sv, "Accept-Encoding"sv, "Referer"sv, "Origin"sv,
    "Sec-Fetch-Dest"sv, "Sec-Fetch-Mode"sv, "Sec-Fetch-Site"sv, "Sec-Fetch-User"sv,
};

void apply_request_header_overrides(FetchParams const& params)
{
    auto* record = params.engine_record();
    if (!record)
        return;
    auto& overrides = record->request_header_overrides(params.is_cors_preflight());
    if (!overrides)
        return;
    auto headers = params.request()->header_list();
    for (auto name : generated_headers) {
        if (overrides->contains(name))
            headers->delete_(name);
    }
    for (auto const& header : *overrides)
        headers->append(header);
}

void remove_content_length_override(FetchParams const& params)
{
    if (auto* record = params.engine_record()) {
        if (auto& overrides = record->request_header_overrides(params.is_cors_preflight())) {
            params.request()->header_list()->delete_("Content-Length"sv);
            overrides->delete_("Content-Length"sv);
        }
    }
}

static void modify_request(FetchParams const& params, Request* original, ModifyRequest overrides)
{
    auto request = params.request();
    auto& generated = params.engine_record()->request_header_overrides(params.is_cors_preflight());
    if (overrides.headers.has_value()) {
        generated = HTTP::HeaderList::create();
        for (auto const& header : *overrides.headers) {
            if (any_of(generated_headers, [&](auto name) { return header.name.equals_ignoring_ascii_case(name); }))
                generated->append(header);
        }
    }
    auto apply = [&](Request& target, bool prepared) {
        if (overrides.url.has_value())
            target.url_list().last() = *overrides.url;
        if (overrides.method.has_value())
            target.set_method(*overrides.method);
        if (overrides.headers.has_value()) {
            target.set_header_list(HTTP::HeaderList::create(*overrides.headers));
            if (!prepared) {
                for (auto name : generated_headers)
                    target.header_list()->delete_(name);
            }
        }
        if (overrides.cookie.has_value())
            target.header_list()->set({ "Cookie"sv, *overrides.cookie });
    };
    if (original && original != request.ptr())
        apply(*original, false);
    apply(*request, true);
    if (overrides.body.has_value()) {
        if (generated)
            generated->delete_("Content-Length"sv);
        auto body = FetchBody::from_bytes(Core::ImmutableBytes::adopt(overrides.body.release_value()));
        if (original && original != request.ptr()) {
            original->set_body(body);
            original->header_list()->delete_("Content-Length"sv);
        }
        request->set_body(body);
        request->header_list()->set({ "Content-Length"sv, ByteString::number(*body.length()) });
    }
}

static void modify_response(Response& response, ModifyResponse overrides)
{
    if (overrides.status.has_value())
        response.set_status(*overrides.status);
    if (overrides.reason.has_value())
        response.set_status_message(overrides.reason.release_value());
    bool replace_headers = overrides.headers.has_value();
    if (replace_headers)
        response.set_header_list(HTTP::HeaderList::create(overrides.headers.release_value()));
    if (overrides.cookies.has_value()) {
        auto headers = HTTP::HeaderList::create(response.header_list()->headers());
        if (!replace_headers)
            headers->delete_("Set-Cookie"sv);
        for (auto const& cookie : *overrides.cookies)
            headers->append(cookie);
        response.set_header_list(move(headers));
    }
}

void reach_network_phase(FetchParams const& params, NetworkPhase phase, RefPtr<Response> response, RefPtr<Request> original, ResumeNetworkFetch resume)
{
    auto event = snapshot_network_activity(preflight_controller(params), *params.request(), *params.timing_info(), response.ptr(), FetchNetworkActivity::PhaseReached { phase, {} });
    params.engine_record()->reach_network_phase(move(event), [params = NonnullRefPtr { params }, original = move(original), response = move(response), resume = move(resume)](NetworkDecision decision) mutable {
        auto result = decision.visit(
            [](ContinueFetch) { return NetworkResult::Continue; },
            [&](FailFetch) {
                if (response)
                    params->discard_response_body(*response);
                response = params->is_canceled() ? Response::appropriate_network_error(*params) : Response::network_error("Intercepted fetch failed"_string);
                return NetworkResult::Fail;
            },
            [&](ModifyRequest& overrides) {
                modify_request(*params, original.ptr(), move(overrides));
                report_network_activity(*params, nullptr, FetchNetworkActivity::RequestUpdated {});
                return NetworkResult::RequestModified;
            },
            [&](ModifyResponse& overrides) {
                modify_response(*response, move(overrides));
                return NetworkResult::Continue;
            },
            [&](ProvideResponse& provide) {
                if (!response) {
                    response = Response::create();
                    response->set_url_list(params->request()->url_list());
                }
                modify_response(*response, move(provide.response));
                if (provide.body.has_value()) {
                    params->discard_response_body(*response);
                    auto replacement = Response::create();
                    replacement->set_url_list(response->url_list());
                    replacement->set_status(response->status());
                    replacement->set_status_message(response->status_message());
                    replacement->set_header_list(response->header_list());
                    auto body = ResponseBody::from_bytes(Core::ImmutableBytes::adopt(provide.body.release_value()));
                    auto info = replacement->body_info();
                    info.encoded_size = info.decoded_size = *body.length();
                    replacement->set_body_info(info);
                    replacement->set_body(move(body));
                    response = move(replacement);
                }
                return NetworkResult::ProvideResponse;
            },
            [&](Authenticate& auth) {
                modify_response(*response, move(auth.response));
                params->request()->header_list()->set({ "Authorization"sv, move(auth.authorization) });
                return NetworkResult::ProvideCredentials;
            },
            [](CancelAuthentication) { return NetworkResult::CancelAuthentication; },
            [](ConfigureRequest&) -> NetworkResult { VERIFY_NOT_REACHED(); });
        resume(result, move(response));
    });
}

}
