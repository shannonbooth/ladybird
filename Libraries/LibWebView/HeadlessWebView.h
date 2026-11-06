/*
 * Copyright (c) 2024-2025, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullOwnPtr.h>
#include <AK/Utf16String.h>
#include <AK/Vector.h>
#include <AK/WeakPtr.h>
#include <AK/Weakable.h>
#include <LibCore/AnonymousBuffer.h>
#include <LibCore/Forward.h>
#include <LibGfx/Forward.h>
#include <LibWebCommon/PixelUnits.h>
#include <LibWebView/Forward.h>
#include <LibWebView/PendingReply.h>
#include <LibWebView/TabObserver.h>
#include <LibWebView/ViewImplementation.h>
#include <LibWebView/ViewSurface.h>

namespace WebView {

// A window that is never shown, which shows a tab for as long as it lives.
class WEBVIEW_API HeadlessWebView
    : public ViewSurface
    , public TabObserver
    , public Weakable<HeadlessWebView> {
public:
    AK_ALLOC_WITH_KMALLOC;

    virtual ~HeadlessWebView() override;

    static NonnullOwnPtr<HeadlessWebView> create(Core::AnonymousBuffer theme, Web::DevicePixelSize window_size, IsPrivate = IsPrivate::No);
    static NonnullOwnPtr<HeadlessWebView> create_child(HeadlessWebView& parent, CanonicalTraversable&);
    static NonnullOwnPtr<PictureInPictureWindow> create_picture_in_picture_window(CanonicalTraversable&, ViewImplementation const& owner, Gfx::IntSize video_size);

    void reset_viewport_size(Web::DevicePixelSize);

    void close_child_web_views();
    // The root window owns every window opened from its windows. Once its own tab is gone, the first of them owns the
    // others, and is returned for its owner to own in turn.
    OwnPtr<HeadlessWebView> hand_child_web_views_to_new_root();
    // Child crashes then no longer reach this window. The children stay, as deferred tasks may still reach them.
    void disconnect_child_crash_handlers();

    virtual ViewImplementation* create_view_for_new_tab_or_window(IsPrivate) override;

protected:
    HeadlessWebView(Core::AnonymousBuffer theme, Web::DevicePixelSize viewport_size, ViewImplementation&);

    static ViewImplementation& open_tab(IsPrivate);

    HeadlessWebView& adopt_child_web_view(NonnullOwnPtr<HeadlessWebView>);

    RefPtr<DialogReply> m_pending_dialog_reply;
    Optional<Utf16String> m_pending_prompt_text;

protected:
    // ^TabObserver
    virtual void tab_was_removed_from_user_interface() override;
    virtual void tab_web_content_did_crash(ViewImplementation::WebContentCrashReason) override;

private:
    void propagate_web_content_crash(ViewImplementation::WebContentCrashReason);
    void discard_child_web_view(HeadlessWebView&);
    void schedule_forced_close();

    Core::AnonymousBuffer m_theme;
    Web::DevicePixelSize m_viewport_size;

    // When restoring from fullscreen, we need to know to what dimension.
    Web::DevicePixelRect m_previous_dimensions;

    RefPtr<Core::Timer> m_forced_close_timer;
    WeakPtr<HeadlessWebView> m_parent_web_view;
    bool m_propagate_crashes_to_parent { true };
    Vector<NonnullOwnPtr<HeadlessWebView>> m_child_web_views;
};

}
