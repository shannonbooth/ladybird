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
#include <LibURL/URL.h>
#include <LibWebView/AudibilityHold.h>
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
        ContentBlocking };
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

    void set_title(Utf16String);
    void set_url(URL::URL);
    void set_favicon(Optional<Gfx::Bitmap const&>);
    void set_loading(bool);
    void update_audibility();
    void page_left(WebContentPage const&);
    void page_began_hosting(Web::PageId id) { m_audibility_hold.page_began_hosting(id); }
    void page_closed(Web::PageId);
    void did_block_resource(Web::PageId, URL::URL const&, StringView kind);
    void reset_blocked_resources();

    // The attached frontend observes document state; it does not own it.
    Function<void(Change)> on_change;

private:
    void changed(Change change)
    {
        if (on_change)
            on_change(change);
    }
    CanonicalTraversable& m_traversable;
    Utf16String m_title;
    URL::URL m_url { URL::about_blank() };
    Optional<String> m_favicon_hash;
    RefPtr<Gfx::Bitmap const> m_favicon;
    bool m_is_loading { false };
    Audio::Audibility m_audibility { Audio::Audibility::Inaudible };
    u64 m_audibility_change_count { 0 };
    AudibilityHold m_audibility_hold;
    RefPtr<Core::Timer> m_audibility_hold_timer;
    Vector<BlockedResource> m_blocked_resources;
    HashMap<Web::PageId, u64> m_blocked_resource_counts;
};

}
