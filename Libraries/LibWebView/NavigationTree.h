/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibIPC/Forward.h>
#include <LibURL/Origin.h>
#include <LibURL/URL.h>
#include <LibWebView/Export.h>
#include <LibWebView/Forward.h>

namespace WebView {

struct WEBVIEW_API NavigationTreeSnapshotNode {
    String id;
    Optional<String> parent_id;
    Optional<URL::URL> active_url;
    Optional<URL::Origin> active_origin;
    String target_name;
    bool is_local_to_receiver { false };
};

struct WEBVIEW_API NavigationTreeSnapshot {
    Vector<NavigationTreeSnapshotNode> nodes;
};

}

namespace IPC {

template<>
WEBVIEW_API ErrorOr<void> encode(Encoder&, WebView::NavigationTreeSnapshotNode const&);

template<>
WEBVIEW_API ErrorOr<WebView::NavigationTreeSnapshotNode> decode(Decoder&);

template<>
WEBVIEW_API ErrorOr<void> encode(Encoder&, WebView::NavigationTreeSnapshot const&);

template<>
WEBVIEW_API ErrorOr<WebView::NavigationTreeSnapshot> decode(Decoder&);

}
