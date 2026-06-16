/*
 * Copyright (c) 2026, Ladybird contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <AK/RefPtr.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <LibGfx/Cursor.h>
#include <LibURL/URL.h>
#include <LibWeb/Compositor/Types.h>
#include <LibWeb/Page/Page.h>
#include <LibWeb/PixelUnits.h>
#include <LibWebView/Forward.h>

namespace WebView {

class WEBVIEW_API SiteIsolationManager {
public:
    static SiteIsolationManager& the();

    enum class ChildFrameOwner : u8 {
        Local,
        Remote,
    };

    struct PendingChildFrameNavigation {
        URL::URL target_url;
        ChildFrameOwner target_owner { ChildFrameOwner::Local };
        Optional<u64> remote_page_id;
    };

    struct ChildFrameHost {
        String parent_frame_id;
        Optional<URL::URL> last_committed_url;
        Optional<PendingChildFrameNavigation> pending_navigation;
        Optional<Web::DevicePixelRect> viewport_rect;
        double device_pixel_ratio { 1 };
        ChildFrameOwner owner { ChildFrameOwner::Local };
        RefPtr<WebContentClient> remote_client;
        u64 remote_page_id { 0 };

        bool is_remote() const
        {
            return owner == ChildFrameOwner::Remote && remote_client && remote_page_id != 0;
        }
    };

    static StringView child_frame_owner_name(ChildFrameOwner);

    Web::NavigationProcessDecision decide_navigation_process(WebContentClient&, u64 page_id, Optional<String> frame_id, URL::URL current_url, URL::URL target_url, Web::NavigationTarget);

    void did_create_child_frame(u64 page_id, String parent_frame_id, String frame_id);
    void did_update_child_frame_viewport(u64 page_id, String frame_id, Web::DevicePixelRect viewport_rect, double device_pixel_ratio);
    void did_destroy_child_frame(WebContentClient&, u64 page_id, StringView frame_id);
    bool update_child_frame_committed_url(WebContentClient& committing_client, u64 page_id, StringView frame_id, URL::URL const&);
    bool remote_child_frame_did_finish_loading(WebContentClient& remote_client, u64 remote_page_id, URL::URL const&);

    Optional<ChildFrameHost&> child_frame(u64 page_id, StringView frame_id);
    Optional<ChildFrameHost const&> child_frame(u64 page_id, StringView frame_id) const;
    template<CallableAs<IterationDecision, String const&, ChildFrameHost const&> Callback>
    void for_each_child_frame(u64 page_id, Callback callback) const;

    bool has_matching_pending_child_frame_navigation(u64 page_id, StringView frame_id, URL::URL const&, ChildFrameOwner) const;
    void record_pending_child_frame_navigation(u64 page_id, StringView frame_id, URL::URL const&, ChildFrameOwner, Optional<u64> remote_page_id = {});
    void clear_pending_child_frame_navigation(u64 page_id, StringView frame_id);

    void transition_child_frame_to_remote(WebContentClient& parent_client, u64 page_id, StringView frame_id, RefPtr<WebContentClient>, u64 remote_page_id, URL::URL const&);
    void transition_child_frame_to_local(WebContentClient& parent_client, u64 page_id, StringView frame_id);
    void close_remote_child_frames_for_page(WebContentClient&, u64 page_id);
    void close_all_remote_child_frames(WebContentClient&);
    void remove_page(u64 page_id);
    void remove_all_pages_for_client(WebContentClient&);

    Optional<String> focused_child_frame(u64 page_id) const;
    void set_focused_child_frame(u64 page_id, String const& frame_id);
    void clear_focused_child_frame(u64 page_id);

    bool dispatch_cursor_change_to_parent_frame(WebContentClient& remote_client, u64 page_id, Gfx::Cursor const&);

    String process_tree_for_testing(Optional<u64> root_page_id = {});

private:
    SiteIsolationManager() = default;

    struct ParentFrame {
        WebContentClient* parent_client { nullptr };
        u64 page_id { 0 };
        String frame_id;
        ChildFrameHost* child_frame { nullptr };
    };

    static bool client_owns_page(WebContentClient const&, u64 page_id);
    Optional<ParentFrame> parent_frame_for_remote_page(WebContentClient&, u64 page_id);
    URL::URL document_url_for_page(WebContentClient&, u64 page_id, URL::URL const& fallback_url);
    Optional<URL::URL> document_url_for_child_frame(ChildFrameHost const&);
    URL::URL embedding_page_url_for_child_frame_navigation(WebContentClient&, u64 page_id, ChildFrameHost const&, URL::URL const&);
    ChildFrameOwner expected_child_frame_owner_for_navigation(WebContentClient&, u64 page_id, ChildFrameHost const&, URL::URL const&);
    bool is_child_frame_commit_allowed(WebContentClient& parent_client, u64 page_id, StringView frame_id, ChildFrameHost const&, URL::URL const&, ChildFrameOwner committing_owner);
    void update_remote_child_frame_process_title(WebContentClient& parent_client, WebContentClient& remote_client, URL::URL const&);
    void update_remote_child_frame_process_title(WebContentClient& parent_client, ChildFrameHost const&, URL::URL const* = nullptr);

    HashMap<u64, HashMap<String, ChildFrameHost>> m_child_frames;
    HashMap<u64, String> m_focused_child_frames;
};

template<CallableAs<IterationDecision, String const&, SiteIsolationManager::ChildFrameHost const&> Callback>
void SiteIsolationManager::for_each_child_frame(u64 page_id, Callback callback) const
{
    auto child_frames = m_child_frames.get(page_id);
    if (!child_frames.has_value())
        return;

    for (auto const& entry : *child_frames) {
        if (callback(entry.key, entry.value) == IterationDecision::Break)
            return;
    }
}

}
