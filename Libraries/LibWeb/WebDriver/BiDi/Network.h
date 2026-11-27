/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/HashTable.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/Optional.h>
#include <AK/RefCounted.h>
#include <AK/RefPtr.h>
#include <AK/String.h>
#include <AK/Utf16String.h>
#include <LibGC/Forward.h>
#include <LibGC/Ptr.h>
#include <LibHTTP/HeaderList.h>
#include <LibWeb/Fetch/Engine/NetworkActivity.h>
#include <LibWeb/Fetch/Infrastructure/NetworkObservation.h>
#include <LibWeb/Forward.h>
#include <LibWebCommon/HTML/CrossProcessId.h>
#include <LibWebCommon/WebDriver/Response.h>

namespace Web::WebDriver::BiDi {

// https://w3c.github.io/webdriver-bidi/#request-id
// NB: Fetch's internal request clones and redirects share this identity.
class NetworkRequest final : public Fetch::Infrastructure::NetworkObserverState {
    AK_ALLOC_WITH_KMALLOC;

public:
    static NonnullRefPtr<NetworkRequest> create() { return adopt_ref(*new NetworkRequest); }

    String id;
    // The navigable the request's events go to.
    Optional<HTML::CrossProcessId> navigable_id;
    Optional<String> navigation;
    HashTable<u8> before_request_sent;
    HashTable<u8> response_started;
    HashTable<u8> completed;
    HashTable<u8> transferred;
    HashMap<u8, JsonObject> request_data;

private:
    NetworkRequest();
};

WEB_API bool network_events_enabled();
void network_session_state_changed();
void receive_network_activity(Fetch::Infrastructure::FetchController& owner, Fetch::Infrastructure::FetchController& source, Fetch::Engine::Event::FetchNetworkActivity const&);
void forget_interceptions(Fetch::Infrastructure::FetchController const&);
WEB_API Response continue_network_request(StringView method, JsonObject const&);

Optional<JsonObject> network_response_for_navigation(Fetch::Infrastructure::Request const&, Fetch::Infrastructure::FetchTimingInfo const&,
    Fetch::Infrastructure::Response const&);
void network_navigation_response_finished(HTML::Navigable const&, JsonObject const&, Fetch::Infrastructure::Response const&, Fetch::Infrastructure::FetchTimingInfo const*, Optional<StringView> error = {});

}
