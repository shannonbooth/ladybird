/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibIPC/Forward.h>
#include <LibURL/Origin.h>
#include <LibURL/URL.h>
#include <LibWeb/Forward.h>
#include <LibWeb/HTML/SandboxingFlagSet.h>
#include <LibWeb/HTML/SerializedPolicyContainer.h>
#include <LibWeb/ReferrerPolicy/ReferrerPolicy.h>

namespace Web::HTML {

// AD-HOC: When a navigation's document is hosted by another WebContent process, the source document only exists in
//         the process where the navigation started. This carries the source snapshot params it snapshotted, so the
//         hosting process can populate the entry's document as if it had snapshotted them locally.
struct NavigationSourceSnapshot {
    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#source-snapshot-params
    // NB: The source snapshot params' fetch client cannot cross the process boundary. The receiving process
    //     substitutes its own environment as the request client.
    bool has_transient_activation { false };
    SandboxingFlagSet sandboxing_flags {};
    bool allows_downloading { true };
    SerializedPolicyContainer source_policy_container;
};

}

namespace IPC {

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::HTML::NavigationSourceSnapshot const&);

template<>
WEB_API ErrorOr<Web::HTML::NavigationSourceSnapshot> decode(Decoder&);

}
