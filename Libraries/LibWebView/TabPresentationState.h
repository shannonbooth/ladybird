/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/RefPtr.h>
#include <AK/Utf16String.h>
#include <LibAudio/Audibility.h>
#include <LibCore/Forward.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/Color.h>
#include <LibURL/URL.h>
#include <LibWebCommon/Page/ScreenWakeLockState.h>
#include <LibWebView/ContentBlocking.h>
#include <LibWebView/Export.h>
#include <LibWebView/Forward.h>

namespace WebView {

class WEBVIEW_API TabPresentationState {
public:
    enum class Change { Title,
        URL,
        Favicon,
        Loading,
        Audibility,
        ContentBlocking,
        ThemeColor,
        BackgroundColor,
        ResourceCount,
        ScreenWakeLock,
        TextFragmentIndication,
    };
    explicit TabPresentationState(CanonicalTraversable&);
    ~TabPresentationState();

    Utf16String const& title() const { return m_title; }
    URL::URL const& url() const { return m_url; }
    Optional<String> const& favicon_hash() const { return m_favicon_hash; }
    RefPtr<Gfx::Bitmap const> favicon() const { return m_favicon; }
    bool is_loading() const { return m_is_loading; }
    Audio::Audibility audibility() const { return m_audibility; }
    u64 audibility_change_count() const { return m_audibility_change_count; }
    Vector<BlockedResource> const& blocked_resources() const { return m_blocked_resources; }
    u64 blocked_resource_count() const;
    Optional<Gfx::Color> theme_color() const { return m_theme_color; }
    Optional<Gfx::Color> background_color() const { return m_background_color; }
    i32 resource_count() const { return m_resource_count; }
    Web::ScreenWakeLockState screen_wake_lock_state() const { return m_screen_wake_lock_state; }
    bool text_fragment_indication_is_visible() const { return m_text_fragment_indication_is_visible; }

    void set_title(Utf16String);
    void set_url(URL::URL);
    void set_favicon(Optional<Gfx::Bitmap const&>);
    void set_loading(bool);
    void update_audibility();
    void page_closed(Web::PageId);
    void did_block_resource(Web::PageId, URL::URL const&, StringView kind);
    void reset_blocked_resources();
    void set_screen_wake_lock_state(Web::ScreenWakeLockState);
    void set_text_fragment_indication_visible(bool);

    // What the page hosting the top-level document reports of it. Another page's report is not about the tab.
    void did_change_theme_color(WebContentPage const&, Gfx::Color);
    void did_change_background_color(WebContentPage const&, Gfx::Color);
    void did_update_resource_count(WebContentPage const&, i32 count_waiting);
    void did_change_screen_wake_lock_state(WebContentPage const&, Web::ScreenWakeLockState);
    void did_change_text_fragment_indication_visibility(WebContentPage const&, bool visible);

    // The attached frontend observes document state; it does not own it.
    Function<void(Change)> on_change;

private:
    void changed(Change change)
    {
        if (on_change)
            on_change(change);
    }
    bool hosts_top_level_document(WebContentPage const&) const;
    CanonicalTraversable& m_traversable;
    Utf16String m_title;
    URL::URL m_url { URL::about_blank() };
    Optional<String> m_favicon_hash;
    RefPtr<Gfx::Bitmap const> m_favicon;
    bool m_is_loading { false };
    Audio::Audibility m_audibility { Audio::Audibility::Inaudible };
    u64 m_audibility_change_count { 0 };
    Vector<BlockedResource> m_blocked_resources;
    HashMap<Web::PageId, u64> m_blocked_resource_counts;
    Optional<Gfx::Color> m_theme_color;
    Optional<Gfx::Color> m_background_color;
    i32 m_resource_count { 0 };
    Web::ScreenWakeLockState m_screen_wake_lock_state { Web::ScreenWakeLockState::Released };
    bool m_text_fragment_indication_is_visible { false };
};

}
