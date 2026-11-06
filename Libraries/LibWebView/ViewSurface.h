/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/Vector.h>
#include <LibCore/AnonymousBuffer.h>
#include <LibGfx/Point.h>
#include <LibGfx/Rect.h>
#include <LibWebCommon/Clipboard/SystemClipboard.h>
#include <LibWebCommon/PixelUnits.h>
#include <LibWebCommon/WebView/AccessibilityNodeData.h>
#include <LibWebView/BrowsingSession.h>
#include <LibWebView/Export.h>
#include <LibWebView/Forward.h>

namespace WebView {

struct ScreenRects {
    Vector<Web::DevicePixelRect> rects;
    // The available rects leave out what the system keeps for itself, such as a menu bar or a dock.
    Vector<Web::DevicePixelRect> available_rects;
    u32 main_screen_index { 0 };
};

// What a frontend widget shows a tab on. The traversable owns the tab, and the frontend deletes the widget no later
// than when the tab is removed from the user interface, so that a surface never outlives the tab it shows.
class WEBVIEW_API ViewSurface {
public:
    virtual ~ViewSurface();

    bool is_attached() const { return m_view != nullptr; }
    ViewImplementation& tab() const;
    // Stops showing the tab, before the widget goes away, while what its callbacks reach still exists.
    void release_tab();

    virtual void did_update_zoom() { }
    virtual void show_context_menu(Menu&, Gfx::IntPoint /* content_position */) { }
    virtual void did_accept_presented_backing_store(i32, Gfx::IntRect) { }
    // Returns whether the surface took over releasing the backing store.
    virtual bool release_backing_store(i32) { return false; }
    virtual void did_get_accessibility_tree(u64, Vector<AccessibilityNodeData>) { }
    virtual void did_change_accessibility_tree(u64, u64, AccessibilityTreeDelta) { }
    virtual void did_change_accessibility_focus(i64) { }

    virtual Web::Clipboard::SystemClipboardItem clipboard_item() const;
    virtual void insert_clipboard_item(Web::Clipboard::SystemClipboardItem);

    // The tab to show a tab or window opened from this one in, or null for the application to open one of its own.
    virtual ViewImplementation* create_view_for_new_tab_or_window(IsPrivate) { return nullptr; }

private:
    friend class ViewImplementation;
    ViewImplementation* m_view { nullptr };
};

}
