/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/NavigationTree.h>

#include <LibIPC/Decoder.h>
#include <LibIPC/Encoder.h>

namespace IPC {

template<>
ErrorOr<void> encode(Encoder& encoder, WebView::NavigationTreeSnapshotNode const& node)
{
    TRY(encoder.encode(node.id));
    TRY(encoder.encode(node.parent_id));
    TRY(encoder.encode(node.active_url));
    TRY(encoder.encode(node.active_origin));
    TRY(encoder.encode(node.target_name));
    TRY(encoder.encode(node.is_local_to_receiver));
    return {};
}

template<>
ErrorOr<WebView::NavigationTreeSnapshotNode> decode(Decoder& decoder)
{
    auto id = TRY(decoder.decode<String>());
    auto parent_id = TRY(decoder.decode<Optional<String>>());
    auto active_url = TRY(decoder.decode<Optional<URL::URL>>());
    auto active_origin = TRY(decoder.decode<Optional<URL::Origin>>());
    auto target_name = TRY(decoder.decode<String>());
    auto is_local_to_receiver = TRY(decoder.decode<bool>());

    return WebView::NavigationTreeSnapshotNode {
        .id = move(id),
        .parent_id = move(parent_id),
        .active_url = move(active_url),
        .active_origin = move(active_origin),
        .target_name = move(target_name),
        .is_local_to_receiver = is_local_to_receiver,
    };
}

template<>
ErrorOr<void> encode(Encoder& encoder, WebView::NavigationTreeSnapshot const& snapshot)
{
    TRY(encoder.encode(snapshot.nodes));
    return {};
}

template<>
ErrorOr<WebView::NavigationTreeSnapshot> decode(Decoder& decoder)
{
    auto nodes = TRY(decoder.decode<Vector<WebView::NavigationTreeSnapshotNode>>());
    return WebView::NavigationTreeSnapshot { move(nodes) };
}

}
