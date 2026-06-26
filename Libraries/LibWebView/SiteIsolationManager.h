/*
 * Copyright (c) 2026-present, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <AK/RefPtr.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <LibURL/Origin.h>
#include <LibURL/URL.h>
#include <LibWeb/Bindings/Navigation.h>
#include <LibWeb/HTML/Navigable.h>
#include <LibWeb/HTML/POSTResource.h>
#include <LibWeb/HTML/StructuredSerialize.h>
#include <LibWeb/Page/EventResult.h>
#include <LibWeb/Page/InputEvent.h>
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

    struct RemoteChildFrameInputTarget {
        RefPtr<WebContentClient> remote_client;
        u64 remote_page_id { 0 };
        Web::DevicePixelRect viewport_rect;
    };

    Web::NavigationProcessDecision decide_navigation_process(WebContentClient&, u64 page_id, Optional<String> frame_id, URL::URL current_url, URL::URL target_url, Web::NavigationTarget, Optional<URL::Origin> current_origin = {});

    void did_create_child_frame(u64 page_id, String parent_frame_id, String frame_id);
    void did_update_child_frame_viewport(u64 page_id, String frame_id, Web::DevicePixelRect viewport_rect, double device_pixel_ratio);
    bool did_commit_child_frame_navigation(WebContentClient&, u64 page_id, StringView frame_id, URL::URL const& url, Web::HTML::RemoteNavigableDescriptor);
    bool navigate_remote_child_frame(WebContentClient&, u64 page_id, String frame_id, URL::URL, Variant<Empty, String, Web::HTML::POSTResource>, Web::Bindings::NavigationHistoryBehavior);
    void did_destroy_child_frame(WebContentClient&, u64 page_id, StringView frame_id);
    Optional<RemoteChildFrameInputTarget> remote_child_frame_input_target_at(u64 page_id, Web::DevicePixelPoint) const;
    bool remote_child_frame_did_commit_navigation(WebContentClient& remote_client, u64 remote_page_id, URL::URL const&, Optional<Web::HTML::RemoteNavigableDescriptor> = {});
    bool remote_child_frame_did_finish_loading(WebContentClient& remote_client, u64 remote_page_id, URL::URL const&);
    bool remote_child_frame_did_finish_handling_input_event(WebContentClient& remote_client, u64 remote_page_id, Web::EventResult);
    bool did_post_message_to_remote_navigable(WebContentClient&, u64 page_id, String target_navigable_id, String source_navigable_id, Web::HTML::SerializedTransferRecord, Variant<String, URL::Origin>, URL::Origin);
    bool did_request_remote_window_operation(WebContentClient&, u64 page_id, String target_navigable_id, Web::HTML::RemoteWindowOperation);
    void did_update_remote_navigable(WebContentClient&, u64 page_id, Web::HTML::RemoteNavigableDescriptor);
    void did_register_blob_url(WebContentClient&, u64 page_id, String url, URL::BlobURLEntry);
    void did_revoke_blob_url(WebContentClient&, u64 page_id, String url);
    void remove_page(u64 page_id);
    void remove_all_pages_for_client(WebContentClient&);
    String dump_process_tree(WebContentClient&, u64 page_id) const;
    HashMap<pid_t, pid_t> remote_frame_process_embedders() const;

    bool has_matching_pending_child_frame_navigation(u64 page_id, StringView frame_id, URL::URL const&, ChildFrameOwner) const;
    void record_pending_child_frame_navigation(u64 page_id, StringView frame_id, URL::URL const&, ChildFrameOwner, Optional<u64> remote_page_id = {});
    void clear_pending_child_frame_navigation(u64 page_id, StringView frame_id);
    void transition_child_frame_to_remote(WebContentClient& parent_client, u64 page_id, StringView frame_id, RefPtr<WebContentClient>, u64 remote_page_id);
    void transition_child_frame_to_local(WebContentClient& parent_client, u64 page_id, StringView frame_id);
    void close_remote_child_frames_for_page(WebContentClient&, u64 page_id);

    Optional<ChildFrameHost&> child_frame(u64 page_id, StringView frame_id);
    Optional<ChildFrameHost const&> child_frame(u64 page_id, StringView frame_id) const;

    template<CallableAs<IterationDecision, String const&, ChildFrameHost const&> Callback>
    void for_each_child_frame(u64 page_id, Callback callback) const;

private:
    SiteIsolationManager() = default;

    struct ParentFrame {
        WebContentClient* parent_client { nullptr };
        u64 page_id { 0 };
        String frame_id;
        ChildFrameHost* child_frame { nullptr };
    };

    struct RemotePage {
        WebContentClient* client { nullptr };
        u64 page_id { 0 };
    };

    static bool client_owns_page(WebContentClient const&, u64 page_id);
    Optional<ParentFrame> parent_frame_for_remote_page(WebContentClient&, u64 page_id);
    URL::URL document_url_for_page(WebContentClient&, u64 page_id, URL::URL const& fallback_url);
    Optional<URL::URL> document_url_for_child_frame(ChildFrameHost const&);
    URL::URL embedding_page_url_for_child_frame_navigation(WebContentClient&, u64 page_id, ChildFrameHost const&, URL::URL const&);
    void update_remote_navigable_in_remote_descendants(u64 page_id, Web::HTML::RemoteNavigableDescriptor const&, Optional<RemotePage> excluded_remote_page = {});
    void register_blob_url_in_remote_descendants(u64 page_id, String const& url, URL::BlobURLEntry const&, Optional<RemotePage> excluded_remote_page = {});
    void revoke_blob_url_in_remote_descendants(u64 page_id, String const& url, Optional<RemotePage> excluded_remote_page = {});

    HashMap<u64, HashMap<String, ChildFrameHost>> m_child_frames;
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
