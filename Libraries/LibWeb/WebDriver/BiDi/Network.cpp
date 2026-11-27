/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Base64.h>
#include <AK/GenericLexer.h>
#include <AK/JsonArray.h>
#include <AK/NeverDestroyed.h>
#include <AK/Random.h>
#include <AK/StringBuilder.h>
#include <AK/Time.h>
#include <LibRequests/Request.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/Fetch/Engine/FetchExecutor.h>
#include <LibWeb/Fetch/Fetching/NetworkActivity.h>
#include <LibWeb/Fetch/Infrastructure/FetchController.h>
#include <LibWeb/Fetch/Infrastructure/FetchControllerRegistry.h>
#include <LibWeb/HTML/Scripting/Agent.h>
#include <LibWeb/Fetch/Infrastructure/ClientContextSnapshot.h>
#include <LibWeb/Fetch/Infrastructure/FetchParams.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Bodies.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/MIME.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Responses.h>
#include <LibWeb/HTML/LocalNavigable.h>
#include <LibWeb/HighResolutionTime/TimeOrigin.h>
#include <LibWeb/WebDriver/BiDi/BiDi.h>
#include <LibWeb/WebDriver/BiDi/Network.h>

namespace Web::WebDriver::BiDi {

using namespace Fetch::Infrastructure;
using Response = Fetch::Infrastructure::Response;

using namespace Fetch::Engine;
using Event::FetchNetworkActivity;
using Event::ObservedRequest;
using Event::ObservedResponse;

struct BlockedRequest {
    WeakPtr<FetchControllerRegistry> registry;
    FetchControllerToken owner;
    FetchControllerToken source;
    FetchId fetch_id;
    InterceptionPoint point;
    JsonArray intercepts;

    void answer(NetworkDecision decision) const
    {
        if (auto controller = registry ? registry->find(owner) : nullptr)
            controller->started_fetch_executor().post(Command::ResolveNetworkInterception { fetch_id, point, move(decision) });
    }
};

static thread_local bool g_network_events_enabled { false };

bool network_events_enabled()
{
    return g_network_events_enabled;
}

static auto& blocked_requests()
{
    static thread_local NeverDestroyed<HashMap<String, BlockedRequest>> requests;
    return *requests;
}

static RefPtr<NetworkRequest> network_request_of(RefPtr<NetworkObservation> const& observation)
{
    if (!observation)
        return nullptr;
    return static_cast<NetworkRequest*>(observation->observer_state().ptr());
}

static void resume_blocked_request(String id, NetworkDecision decision)
{
    if (auto blocked = blocked_requests().take(id); blocked.has_value())
        blocked->answer(move(decision));
}

void forget_interceptions(FetchController const& controller)
{
    blocked_requests().remove_all_matching([&](auto const&, auto const& blocked) {
        return blocked.owner == controller.token() || blocked.source == controller.token();
    });
}

static StringView phase_name(NetworkPhase phase)
{
    switch (phase) {
    case NetworkPhase::BeforeRequest:
        return "beforeRequestSent"sv;
    case NetworkPhase::ResponseHeaders:
        return "responseStarted"sv;
    case NetworkPhase::Authentication:
        return "authRequired"sv;
    case NetworkPhase::PrepareRequest:
        VERIFY_NOT_REACHED();
    }
    VERIFY_NOT_REACHED();
}

void network_session_state_changed()
{
    auto const& state = session_state();
    g_network_events_enabled = state.is_active && any_of(state.enabled_events, [](auto const& name) { return name.starts_with_bytes("network."sv); });
    bool has_configuration = state.is_active && (!state.network_default_headers.is_empty() || !state.network_user_context_headers.is_empty() || !state.network_navigable_headers.is_empty() || state.network_default_cache_behavior != "default"sv || !state.network_navigable_cache_behavior.is_empty());
    u8 phases = 0;
    if (has_configuration)
        phases |= 1u << to_underlying(NetworkPhase::PrepareRequest);
    if (g_network_events_enabled) {
        for (auto phase : { NetworkPhase::BeforeRequest, NetworkPhase::ResponseHeaders, NetworkPhase::Authentication }) {
            auto name = phase_name(phase);
            if (is_event_enabled(MUST(String::formatted("network.{}", name))) && any_of(state.network_intercepts, [&](auto const& intercept) { return intercept.phases.contains_slow(name); }))
                phases |= 1u << to_underlying(phase);
        }
    }
    HTML::surrounding_agent().network_activity_observers->set_fetch_observation(g_network_events_enabled, phases);
    if (!state.is_active) {
        while (!blocked_requests().is_empty())
            resume_blocked_request(blocked_requests().begin()->key, ContinueFetch {});
    }
}

static GC::Ptr<HTML::LocalNavigable> navigable_of(NetworkRequest const& state)
{
    if (!state.navigable_id.has_value())
        return {};
    return HTML::local_navigable_with_id(*state.navigable_id);
}

static String network_context_id(HTML::Navigable& navigable)
{
    auto id = navigable.top_level_traversable()->id();
    return MUST(String::formatted("{}-{}", id.namespace_id, id.local_id));
}

static ConfigureRequest network_configuration(HTML::Navigable* navigable)
{
    auto const& state = session_state();
    auto behavior = state.network_default_cache_behavior;
    auto headers = state.network_default_headers;
    if (navigable) {
        auto context = network_context_id(*navigable);
        behavior = state.network_navigable_cache_behavior.get(context).value_or(behavior);
        if (auto it = state.network_user_context_headers.find("default"sv); it != state.network_user_context_headers.end())
            headers.extend(it->value);
        if (auto it = state.network_navigable_headers.find(context); it != state.network_navigable_headers.end())
            headers.extend(it->value);
    }
    return { behavior == "bypass"sv, move(headers) };
}

// https://w3c.github.io/webdriver-bidi/#get-the-network-intercepts
static JsonArray get_network_intercepts(NetworkRequest const& state, ObservedRequest const& request, HTML::Navigable& navigable, StringView phase)
{
    JsonArray intercepts;
    if ((phase == "beforeRequestSent"sv && state.before_request_sent.contains(request.redirect_count)) || (phase == "responseStarted"sv && state.response_started.contains(request.redirect_count)))
        return intercepts;
    auto context = network_context_id(navigable);
    auto event_name = MUST(String::formatted("network.{}", phase));
    auto scope = session_state().enabled_event_contexts.find(event_name);
    if (scope == session_state().enabled_event_contexts.end() || (scope->value.has_value() && !scope->value->contains_slow(context)))
        return intercepts;
    // 5. For each intercept id → intercept of session intercepts:
    for (auto const& intercept : session_state().network_intercepts) {
        if (intercept.browsing_contexts.has_value() && !intercept.browsing_contexts->contains_slow(context))
            continue;
        if (!intercept.phases.contains_slow(phase))
            continue;
        if (intercept.url_patterns.is_empty() || any_of(intercept.url_patterns, [&](auto const& pattern) { return pattern.matches(request.url); }))
            MUST(intercepts.append(intercept.id));
    }
    return intercepts;
}

NetworkRequest::NetworkRequest()
    : id(generate_random_uuid())
{
}

// https://w3c.github.io/webdriver-bidi/#serialize-protocol-bytes
static JsonObject serialize_protocol_bytes(StringView bytes)
{
    JsonObject result;
    if (auto string = String::from_utf8(bytes); !string.is_error()) {
        result.set("type"sv, "string"sv);
        result.set("value"sv, string.release_value());
    } else {
        result.set("type"sv, "base64"sv);
        result.set("value"sv, MUST(encode_base64(bytes.bytes())));
    }
    return result;
}

// https://w3c.github.io/webdriver-bidi/#serialize-header
static JsonArray serialize_headers(HTTP::HeaderList const& headers)
{
    JsonArray result;
    for (auto const& header : headers) {
        JsonObject entry;
        // 1. Let name be the result of UTF-8 decode with name bytes.
        entry.set("name"sv, String::from_utf8_with_replacement_character(header.name.view()));
        // 2. Let value be serialize protocol bytes with value bytes.
        entry.set("value"sv, serialize_protocol_bytes(header.value));
        MUST(result.append(move(entry)));
    }
    return result;
}

static u64 headers_size(HTTP::HeaderList const& headers)
{
    u64 size = 2;
    for (auto const& header : headers)
        size += header.name.length() + header.value.length() + 4;
    return size;
}

// https://w3c.github.io/webdriver-bidi/#get-the-fetch-timings
static JsonObject get_the_fetch_timings(FetchTimingInfo const& timings, HighResolutionTime::DOMHighResTimeStamp end_time, Optional<double> time_origin, double epoch_origin)
{
    JsonObject result;
    result.set("timeOrigin"sv, epoch_origin);
    auto convert = [&](double time) {
        if (!time_origin.has_value() || time == 0)
            return 0.0;
        return HighResolutionTime::coarsen_time(time) - *time_origin;
    };
    auto connection = timings.final_connection_timing_info().value_or(ConnectionTimingInfo {});
    result.set("requestTime"sv, convert(timings.start_time()));
    result.set("redirectStart"sv, convert(timings.redirect_start_time()));
    result.set("redirectEnd"sv, convert(timings.redirect_end_time()));
    result.set("fetchStart"sv, convert(timings.post_redirect_start_time()));
    result.set("dnsStart"sv, convert(connection.domain_lookup_start_time));
    result.set("dnsEnd"sv, convert(connection.domain_lookup_end_time));
    result.set("connectStart"sv, convert(connection.connection_start_time));
    result.set("connectEnd"sv, convert(connection.connection_end_time));
    result.set("tlsStart"sv, convert(connection.secure_connection_start_time));
    result.set("requestStart"sv, convert(timings.final_network_request_start_time()));
    result.set("responseStart"sv, convert(timings.final_network_response_start_time()));
    result.set("responseEnd"sv, convert(end_time));
    return result;
}

static JsonObject get_the_fetch_timings(ObservedRequest const& request, FetchTimingInfo const& timings, double end_time)
{
    auto origin = request.time_origin;
    return get_the_fetch_timings(timings, end_time, origin, origin.has_value() ? *origin - HighResolutionTime::estimated_monotonic_time_of_the_unix_epoch() : 0);
}

// https://w3c.github.io/webdriver-bidi/#get-the-request-data
static JsonObject get_the_request_data(NetworkRequest const& state, ObservedRequest const& request, FetchTimingInfo const& timings, HighResolutionTime::DOMHighResTimeStamp end_time)
{
    JsonObject result;
    result.set("request"sv, state.id);
    result.set("url"sv, request.url.serialize());
    result.set("method"sv, MUST(String::from_utf8(request.method.view())));
    // NB: RequestServer sends one field per name, combining repeated values in order.
    auto headers = HTTP::HeaderList::create();
    auto fields = HTTP::HeaderList::create(request.headers);
    for (auto const& header : *fields) {
        if (!headers->contains(header.name))
            headers->append({ header.name, fields->get(header.name).release_value() });
    }
    result.set("headers"sv, serialize_headers(headers));
    // FIXME: Include the cookies attached by RequestServer.
    result.set("cookies"sv, JsonArray {});
    result.set("headersSize"sv, headers_size(headers));
    result.set("bodySize"sv, request.body_size.has_value() ? JsonValue(*request.body_size) : JsonValue {});
    result.set("destination"sv, request.destination);
    result.set("initiatorType"sv, request.initiator_type.has_value() ? JsonValue(*request.initiator_type) : JsonValue {});
    result.set("timings"sv, get_the_fetch_timings(request, timings, end_time));
    return result;
}

// https://w3c.github.io/webdriver-bidi/#extract-challenges
static JsonArray extract_challenges(ObservedResponse const& response)
{
    JsonArray challenges;
    auto header_name = response.status == 407 ? "Proxy-Authenticate"sv : "WWW-Authenticate"sv;
    for (auto const& header : response.headers) {
        if (!header.name.equals_ignoring_ascii_case(header_name))
            continue;
        GenericLexer lexer(header.value.view());
        Optional<JsonObject> challenge;
        while (!lexer.is_eof()) {
            lexer.ignore_while([](char ch) { return ch == ' ' || ch == '\t' || ch == ','; });
            auto name = lexer.consume_while([](char ch) { return HTTP::is_header_name({ &ch, 1 }); });
            lexer.ignore_while([](char ch) { return ch == ' ' || ch == '\t'; });
            if (name.is_empty()) {
                if (!lexer.is_eof())
                    lexer.ignore();
                continue;
            }
            if (!lexer.consume_specific('=')) {
                if (challenge.has_value())
                    MUST(challenges.append(challenge.release_value()));
                challenge = JsonObject {};
                challenge->set("scheme"sv, MUST(String::from_utf8(name)));
                challenge->set("realm"sv, ""sv);
                continue;
            }
            lexer.ignore_while([](char ch) { return ch == ' ' || ch == '\t'; });
            StringBuilder value;
            if (lexer.consume_specific('"')) {
                while (!lexer.is_eof() && lexer.peek() != '"') {
                    if (lexer.consume_specific('\\') && lexer.is_eof())
                        break;
                    value.append(lexer.consume());
                }
                lexer.consume_specific('"');
            } else {
                value.append(lexer.consume_until([](char ch) { return ch == ',' || ch == ' ' || ch == '\t'; }));
            }
            if (challenge.has_value() && name.equals_ignoring_ascii_case("realm"sv))
                challenge->set("realm"sv, String::from_utf8_with_replacement_character(value.string_view()));
        }
        if (challenge.has_value())
            MUST(challenges.append(challenge.release_value()));
    }
    return challenges;
}

// https://w3c.github.io/webdriver-bidi/#get-the-response-data
static JsonObject get_the_response_data(ObservedResponse const& response, FetchTimingInfo const& timings)
{
    JsonObject result;
    auto url = response.url;
    result.set("url"sv, url.has_value() ? url->serialize() : String {});
    ByteString protocol;
    if (timings.final_connection_timing_info().has_value())
        protocol = timings.final_connection_timing_info()->alpn_negotiated_protocol;
    if (protocol.is_empty())
        protocol = response.protocol;
    if (protocol.is_empty())
        protocol = url.has_value() ? url->scheme().to_byte_string() : ByteString {};
    if (protocol == "http/2"sv)
        protocol = "h2"sv;
    else if (protocol == "http/3"sv)
        protocol = "h3"sv;
    result.set("protocol"sv, MUST(String::from_utf8(protocol.view())));
    result.set("status"sv, response.status);
    // INTEROP: Data URL responses have an "OK" status text in Chromium and Firefox.
    auto status_text = response.reason;
    if (status_text.is_empty() && url.has_value() && url->scheme() == "data"sv)
        status_text = "OK"sv;
    result.set("statusText"sv, String::from_utf8_with_replacement_character(status_text.view()));
    result.set("fromCache"sv, response.from_cache);
    auto headers = HTTP::HeaderList::create(response.headers);
    result.set("headers"sv, serialize_headers(*headers));
    auto mime_type = extract_mime_type(*headers);
    result.set("mimeType"sv, mime_type.has_value() ? mime_type->essence() : String {});
    auto header_size = headers_size(*headers);
    result.set("bytesReceived"sv, response.encoded_size + header_size);
    result.set("headersSize"sv, header_size);
    result.set("bodySize"sv, response.encoded_size);
    JsonObject content;
    content.set("size"sv, response.decoded_size);
    result.set("content"sv, move(content));
    if (response.status == 401 || response.status == 407)
        result.set("authChallenges"sv, extract_challenges(response));
    return result;
}

// https://w3c.github.io/webdriver-bidi/#process-a-network-event
static JsonObject process_a_network_event(HTML::Navigable const& navigable, NetworkRequest const& state, ObservedRequest const& request, FetchTimingInfo const& timing_info, HighResolutionTime::DOMHighResTimeStamp end_time)
{
    JsonObject params;
    params.set("context"sv, navigable_id(navigable));
    params.set("userContext"sv, "default"sv);
    auto blocked = blocked_requests().find(state.id);
    params.set("isBlocked"sv, blocked != blocked_requests().end());
    if (blocked != blocked_requests().end())
        params.set("intercepts"sv, blocked->value.intercepts);
    params.set("navigation"sv, state.navigation.has_value() ? JsonValue(*state.navigation) : JsonValue {});
    params.set("redirectCount"sv, request.redirect_count);
    JsonObject request_data;
    if (auto cached = state.request_data.get(request.redirect_count); cached.has_value())
        request_data = *cached;
    else
        request_data = get_the_request_data(state, request, timing_info, end_time);
    request_data.set("timings"sv, get_the_fetch_timings(request, timing_info, end_time));
    params.set("request"sv, move(request_data));
    params.set("timestamp"sv, UnixDateTime::now().milliseconds_since_epoch());
    return params;
}

// https://w3c.github.io/webdriver-bidi/#command-network-continueRequest
// https://w3c.github.io/webdriver-bidi/#update-the-response
static ErrorOr<JsonValue, Error> continue_network_request_impl(StringView method, JsonObject const& params)
{
    TRY(Network::validate_interception_parameters(method, params));
    // 1. Let blocked requests be session's blocked request map.
    // 2. Let request id be command parameters["request"].
    auto id = params.get_string("request"sv).release_value();
    // 3. If blocked requests does not contain request id then return error with error code no such request.
    auto it = blocked_requests().find(id);
    if (it == blocked_requests().end())
        return Error::from_code(ErrorCode::NoSuchRequest, "Blocked request not found"sv);
    auto phase = phase_name(it->value.point.phase);
    if ((method == "network.continueRequest"sv && phase != "beforeRequestSent"sv)
        || (method == "network.continueResponse"sv && phase == "beforeRequestSent"sv)
        || (method == "network.continueWithAuth"sv && phase != "authRequired"sv)
        || (method == "network.failRequest"sv && phase == "authRequired"sv))
        return Error::from_code(ErrorCode::InvalidArgument, "Command does not apply to the blocked phase"sv);
    auto overrides = TRY(Network::deserialize_interception_overrides(method, params));
    NetworkDecision decision = ContinueFetch {};
    if (method == "network.continueRequest"sv) {
        if (overrides.url.has_value() && !overrides.url->scheme().is_one_of("http"sv, "https"sv))
            return Error::from_code(ErrorCode::InvalidArgument, "An intercepted HTTP request requires an HTTP(S) URL"sv);
        if (overrides.url.has_value() || overrides.method.has_value() || overrides.headers.has_value() || overrides.body.has_value() || overrides.cookie_header.has_value())
            decision = ModifyRequest { move(overrides.url), move(overrides.method), move(overrides.headers), move(overrides.cookie_header), move(overrides.body) };
    } else if (method == "network.failRequest"sv) {
        decision = FailFetch {};
    } else {
        ModifyResponse response { overrides.status, move(overrides.reason), move(overrides.headers), move(overrides.set_cookie_headers) };
        if (method == "network.provideResponse"sv) {
            decision = ProvideResponse { move(response), move(overrides.body) };
        } else if (auto credentials = params.get_object("credentials"sv); credentials.has_value() && phase == "authRequired"sv && (method == "network.continueResponse"sv || params.get_string("action"sv) == "provideCredentials"sv)) {
            auto payload = MUST(String::formatted("{}:{}", *credentials->get_string("username"sv), *credentials->get_string("password"sv)));
            decision = Authenticate { ByteString::formatted("Basic {}", MUST(encode_base64(payload.bytes()))), move(response) };
        } else if (method == "network.continueWithAuth"sv && params.get_string("action"sv) == "cancel"sv) {
            decision = CancelAuthentication {};
        } else if (method == "network.continueResponse"sv) {
            decision = move(response);
        }
    }
    resume_blocked_request(id, move(decision));
    return JsonValue {};
}

WebDriver::Response continue_network_request(StringView method, JsonObject const& params)
{
    auto result = continue_network_request_impl(method, params);
    if (result.is_error())
        return result.release_error();
    return result.release_value();
}

void receive_network_activity(FetchController& owner, FetchController& source, FetchNetworkActivity const& event)
{
    auto const* reached = event.activity.get_pointer<FetchNetworkActivity::PhaseReached>();
    auto state = network_request_of(source.network_observation());
    auto const& observation = source.network_observation();
    auto context = observation && observation->navigation_context.has_value() ? observation->navigation_context : event.request.navigable_id;
    auto navigable = state ? navigable_of(*state) : (context.has_value() ? HTML::local_navigable_with_id(*context) : nullptr);
    Optional<BlockedRequest> offer;
    if (reached && reached->interception.has_value()) {
        offer = BlockedRequest { owner.registry(), owner.token(), source.token(), *owner.started_fetch_id(), *reached->interception, {} };
        if (!owner.control_block().is_ongoing() || !source.control_block().is_ongoing()) {
            offer->answer(FailFetch {});
            return;
        }
        if (reached->phase == NetworkPhase::PrepareRequest) {
            offer->answer(network_configuration(navigable.ptr()));
            return;
        }
    }
    if (!network_events_enabled() || !navigable || !source.network_observation()) {
        if (offer.has_value())
            offer->answer(ContinueFetch {});
        return;
    }
    if (!state) {
        state = NetworkRequest::create();
        state->navigable_id = navigable->id();
        state->navigation = observation->navigation;
        observation->set_observer_state(*state);
    }
    auto hop = event.request.redirect_count;
    if (offer.has_value()) {
        auto phase = phase_name(reached->phase);
        offer->intercepts = get_network_intercepts(*state, event.request, *navigable, phase);
        if (!offer->intercepts.is_empty())
            blocked_requests().set(state->id, offer.release_value());
    }
    auto& control = source.control_block();
    auto parameters = [&] { return process_a_network_event(*navigable, *state, event.request, *event.timing, control.end_time()); };
    auto save_request = [&] { state->request_data.set(hop, get_the_request_data(*state, event.request, *event.timing, control.end_time())); };
    auto before_request = [&] {
        if (state->before_request_sent.contains(hop))
            return;
        state->before_request_sent.set(hop);
        save_request();
        if (is_event_enabled("network.beforeRequestSent"sv))
            emit_event(*navigable, "network.beforeRequestSent"_string, parameters());
    };
    auto emit_response = [&](String name) {
        if (!is_event_enabled(name))
            return;
        auto params = parameters();
        params.set("response"sv, get_the_response_data(*event.response, *event.timing));
        emit_event(*navigable, move(name), move(params));
    };
    event.activity.visit(
        [&](FetchNetworkActivity::PhaseReached const& phase) {
            if (phase.phase == NetworkPhase::Authentication) {
                emit_response("network.authRequired"_string);
                return;
            }
            before_request();
            if (phase.phase == NetworkPhase::ResponseHeaders && !state->response_started.contains(hop)) {
                state->response_started.set(hop);
                emit_response("network.responseStarted"_string);
            }
        },
        [&](FetchNetworkActivity::RequestUpdated const&) { save_request(); },
        [&](FetchNetworkActivity::ResponseCompleted const&) {
            if (state->completed.contains(hop) || state->transferred.contains(hop))
                return;
            state->completed.set(hop);
            if (!control.end_time())
                control.set_end_time(HighResolutionTime::unsafe_shared_current_time());
            emit_response("network.responseCompleted"_string);
        },
        [&](FetchNetworkActivity::Failed const& failed) {
            before_request();
            if (auto it = blocked_requests().find(state->id); it != blocked_requests().end() && it->value.source == source.token())
                resume_blocked_request(state->id, FailFetch {});
            if (state->completed.contains(hop) || state->transferred.contains(hop))
                return;
            state->completed.set(hop);
            if (is_event_enabled("network.fetchError"sv)) {
                auto params = parameters();
                params.set("errorText"sv, failed.reason);
                emit_event(*navigable, "network.fetchError"_string, move(params));
            }
        });
    if (offer.has_value())
        offer->answer(ContinueFetch {});
}

Optional<JsonObject> network_response_for_navigation(Request const& request, FetchTimingInfo const& timing_info,
    Response const& response)
{
    if (!g_network_events_enabled)
        return {};
    auto state = network_request_of(request.network_observation());
    if (!state || state->completed.contains(request.redirect_count()))
        return {};
    auto navigable = navigable_of(*state);
    if (!navigable)
        return {};
    state->transferred.set(request.redirect_count());
    JsonObject result;
    result.set("parameters"sv, process_a_network_event(*navigable, *state, Fetch::Fetching::snapshot_request(request), timing_info, timing_info.end_time()));
    result.set("response"sv, get_the_response_data(Fetch::Fetching::snapshot_response(response), timing_info));
    result.set("postRedirectStartTime"sv, timing_info.post_redirect_start_time());
    auto const& client = request.client_snapshot();
    result.set("timeOrigin"sv, client ? JsonValue(client->time_origin) : JsonValue {});
    return result;
}

void network_navigation_response_finished(HTML::Navigable const& navigable, JsonObject const& metadata, Response const& response, FetchTimingInfo const* timing_info, Optional<StringView> error)
{
    auto name = error.has_value() ? "network.fetchError"sv : "network.responseCompleted"sv;
    if (!g_network_events_enabled || !is_event_enabled(name))
        return;
    auto parameters = metadata.get_object("parameters"sv);
    if (!parameters.has_value())
        return;
    auto params = *parameters;
    params.set("timestamp"sv, UnixDateTime::now().milliseconds_since_epoch());
    if (error.has_value()) {
        params.set("errorText"sv, String::from_utf8_with_replacement_character(*error));
    } else {
        auto fallback_timings = FetchTimingInfo::create();
        auto const& timings = timing_info ? *timing_info : *fallback_timings;
        if (auto request = params.get_object("request"sv); request.has_value()) {
            auto old_timings = request->get_object("timings"sv);
            auto epoch_origin = old_timings->get("timeOrigin"sv)->get_double_with_precision_loss().value();
            auto origin = metadata.get("timeOrigin"sv);
            request->set("timings"sv, get_the_fetch_timings(timings, timings.end_time(), origin->is_null() ? Optional<double> {} : origin->get_double_with_precision_loss(), epoch_origin));
        }
        params.set("response"sv, get_the_response_data(Fetch::Fetching::snapshot_response(response), timings));
    }
    emit_event(navigable, MUST(String::from_utf8(name)), move(params));
}

}
