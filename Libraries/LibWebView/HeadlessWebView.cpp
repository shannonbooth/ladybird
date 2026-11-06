/*
 * Copyright (c) 2024-2025, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/EventLoop.h>
#include <LibCore/Timer.h>
#include <LibWebView/CanonicalTraversable.h>
#include <LibWebView/HeadlessWebView.h>
#include <LibWebView/PictureInPictureWindow.h>

namespace WebView {

static Web::DevicePixelRect const screen_rect { 0, 0, 1920, 1080 };
static constexpr auto child_close_timeout_ms = 1000;

// A headless window is never shown, so it only changes size to keep the aspect ratio of its video.
class HeadlessPictureInPictureWindow final : public PictureInPictureWindow {
public:
    AK_ALLOC_WITH_KMALLOC;

    HeadlessPictureInPictureWindow(Gfx::IntSize size, NonnullOwnPtr<HeadlessWebView> web_view)
        : m_size(size)
        , m_web_view(move(web_view))
    {
        report_page_close_of(m_web_view->tab());
    }

    virtual Gfx::IntSize size() const override { return m_size; }
    virtual String handle() const override { return m_web_view->tab().handle(); }
    virtual void hide() override { }

    virtual void set_video_size(Gfx::IntSize video_size) override
    {
        auto size = size_for_video_size(m_size, video_size, screen_rect.size().to_type<int>());
        if (size == m_size)
            return;

        m_size = size;
        m_web_view->reset_viewport_size(size.to_type<Web::DevicePixels>());
        if (on_resize)
            on_resize(size);
    }

private:
    Gfx::IntSize m_size;
    NonnullOwnPtr<HeadlessWebView> m_web_view;
};

NonnullOwnPtr<PictureInPictureWindow> HeadlessWebView::create_picture_in_picture_window(CanonicalTraversable& traversable, ViewImplementation const& owner, Gfx::IntSize video_size)
{
    auto size = PictureInPictureWindow::initial_size(video_size, screen_rect.size().to_type<int>());
    auto theme = owner.system_theme();
    auto& tab = traversable.view();
    traversable.set_system_visibility_state(Web::HTML::VisibilityState::Visible);
    auto web_view = adopt_own(*new HeadlessWebView(move(theme), size.to_type<Web::DevicePixels>(), tab));
    tab.activate();
    return make<HeadlessPictureInPictureWindow>(size, move(web_view));
}

ViewImplementation& HeadlessWebView::open_tab(IsPrivate is_private)
{
    return ViewImplementation::open_tab(is_private, Web::HTML::VisibilityState::Visible);
}

NonnullOwnPtr<HeadlessWebView> HeadlessWebView::create(Core::AnonymousBuffer theme, Web::DevicePixelSize window_size, IsPrivate is_private)
{
    auto web_view = adopt_own(*new HeadlessWebView(move(theme), window_size, open_tab(is_private)));
    web_view->tab().activate();
    return web_view;
}

NonnullOwnPtr<HeadlessWebView> HeadlessWebView::create_child(HeadlessWebView& parent, CanonicalTraversable& traversable)
{
    traversable.set_system_visibility_state(Web::HTML::VisibilityState::Visible);
    auto web_view = adopt_own(*new HeadlessWebView(parent.m_theme, parent.m_viewport_size, traversable.view()));
    web_view->tab().activate();
    return web_view;
}

HeadlessWebView::~HeadlessWebView() = default;

HeadlessWebView::HeadlessWebView(Core::AnonymousBuffer theme, Web::DevicePixelSize viewport_size, ViewImplementation& tab)
    : TabObserver(tab)
    , m_theme(move(theme))
    , m_viewport_size(viewport_size)
{
    tab.attach_surface(*this, *this);
    tab.set_viewport_size(viewport_size);
    tab.set_system_theme(m_theme);
    tab.set_screen_rects({ .rects = { screen_rect }, .available_rects = { screen_rect }, .main_screen_index = 0 });

    // A headless window is exactly its viewport, so every page gets the viewport as its window size.
    tab.set_window_size(viewport_size.to_type<int>());
}

String HeadlessWebView::show_tab_opened_by_page(Web::HTML::ActivateTab, Web::HTML::WebViewHints, CanonicalTraversable& traversable)
{
    return adopt_child_web_view(create_child(*this, traversable)).tab().handle();
}

void HeadlessWebView::reposition_window(Gfx::IntPoint position)
{
    m_previous_dimensions.set_location(position.template to_type<Web::DevicePixels>());
    this->tab().set_window_position(position);
}

void HeadlessWebView::resize_window(Gfx::IntSize size)
{
    m_viewport_size = size.template to_type<Web::DevicePixels>();

    this->tab().set_window_size(size);
    this->tab().set_viewport_size(m_viewport_size);
}

void HeadlessWebView::restore_window()
{
    this->tab().set_system_visibility_state(Web::HTML::VisibilityState::Visible);
}

void HeadlessWebView::minimize_window()
{
    this->tab().set_system_visibility_state(Web::HTML::VisibilityState::Hidden);
}

void HeadlessWebView::maximize_window()
{
    m_viewport_size = screen_rect.size();
    m_previous_dimensions = screen_rect;

    this->tab().set_window_position(screen_rect.location().to_type<int>());
    this->tab().set_window_size(screen_rect.size().to_type<int>());
    this->tab().set_viewport_size(m_viewport_size);
}

void HeadlessWebView::fullscreen_window()
{
    m_previous_dimensions.set_size(m_viewport_size);
    m_viewport_size = screen_rect.size();

    this->tab().set_window_position(screen_rect.location().to_type<int>());
    this->tab().set_window_size(screen_rect.size().to_type<int>());
    this->tab().set_is_fullscreen(Web::ViewportIsFullscreen::Yes);
}

void HeadlessWebView::exit_fullscreen_window()
{
    m_viewport_size = m_previous_dimensions.size();

    this->tab().set_window_position(m_previous_dimensions.location().to_type<int>());
    this->tab().set_window_size(m_previous_dimensions.size().to_type<int>());
    this->tab().set_is_fullscreen(Web::ViewportIsFullscreen::No);
}

void HeadlessWebView::request_alert(Utf16String const&, NonnullRefPtr<DialogReply> reply)
{
    m_pending_dialog_reply = move(reply);
}

void HeadlessWebView::request_confirm(Utf16String const&, NonnullRefPtr<DialogReply> reply)
{
    m_pending_dialog_reply = move(reply);
}

void HeadlessWebView::request_prompt(Utf16String const&, Utf16String const& prompt_text, NonnullRefPtr<DialogReply> reply)
{
    m_pending_dialog_reply = move(reply);
    m_pending_prompt_text = prompt_text;
}

void HeadlessWebView::request_set_prompt_text(Utf16String const& prompt_text)
{
    m_pending_prompt_text = prompt_text;
}

void HeadlessWebView::request_accept_dialog()
{
    VERIFY(m_pending_dialog_reply);
    m_pending_dialog_reply.release_nonnull()->complete(true, move(m_pending_prompt_text));
}

void HeadlessWebView::request_dismiss_dialog()
{
    VERIFY(m_pending_dialog_reply);
    m_pending_dialog_reply.release_nonnull()->complete(false, {});
    m_pending_prompt_text.clear();
}

void HeadlessWebView::close_child_web_views()
{
    for (auto& child : m_child_web_views) {
        // Children sharing a crashed WebContent process are discarded by their pending crash callbacks.
        if (!child->is_attached())
            continue;
        if (!child->tab().handle().is_empty() && child->tab().page().is_open()) {
            child->tab().request_close();
            child->schedule_forced_close();
        }
    }
}

void HeadlessWebView::disconnect_child_crash_handlers()
{
    for (auto& child : m_child_web_views)
        child->m_propagate_crashes_to_parent = false;
}

void HeadlessWebView::propagate_web_content_crash(ViewImplementation::WebContentCrashReason crash_reason)
{
    if (!m_propagate_crashes_to_parent)
        return;

    if (m_parent_web_view) {
        m_parent_web_view->propagate_web_content_crash(crash_reason);
        return;
    }

    if (is_attached())
        tab().notify_observers([&](TabObserver& observer) { observer.tab_web_content_did_crash(crash_reason); });
}

// A window opened from another goes with its tab. The owner of a root window deletes it.
void HeadlessWebView::tab_was_removed_from_user_interface()
{
    if (auto* root = m_parent_web_view.ptr())
        root->m_child_web_views.remove_first_matching([this](auto const& child) { return child.ptr() == this; });
}

// A crash in a window opened from another reaches the root window too, so that tests waiting on it do not hang.
void HeadlessWebView::tab_web_content_did_crash(ViewImplementation::WebContentCrashReason crash_reason)
{
    auto* root = m_parent_web_view.ptr();
    if (!root)
        return;
    propagate_web_content_crash(crash_reason);
    root->discard_child_web_view(*this);
}

HeadlessWebView& HeadlessWebView::adopt_child_web_view(NonnullOwnPtr<HeadlessWebView> web_view)
{
    // A window outlives the window that opened it, so the root window owns every window opened from its windows.
    if (m_parent_web_view)
        return m_parent_web_view->adopt_child_web_view(move(web_view));

    web_view->m_parent_web_view = make_weak_ptr<HeadlessWebView>();
    m_child_web_views.append(move(web_view));
    return *m_child_web_views.last();
}

OwnPtr<HeadlessWebView> HeadlessWebView::hand_child_web_views_to_new_root()
{
    VERIFY(!m_parent_web_view);
    if (m_child_web_views.is_empty())
        return {};

    auto child_web_views = move(m_child_web_views);
    auto new_root = child_web_views.take_first();
    new_root->m_parent_web_view = nullptr;
    for (auto& child_web_view : child_web_views)
        new_root->adopt_child_web_view(move(child_web_view));
    return new_root;
}

// A headless browser has no tabs or windows, so what the browser UI would open in a new one gets a window of its own.
ViewImplementation* HeadlessWebView::create_view_for_new_tab_or_window(IsPrivate is_private)
{
    return &adopt_child_web_view(create(m_theme, m_viewport_size, is_private)).tab();
}

void HeadlessWebView::discard_child_web_view(HeadlessWebView& child_web_view)
{
    auto* child_web_view_pointer = &child_web_view;
    Core::deferred_invoke([weak_this = make_weak_ptr<HeadlessWebView>(), child_web_view_pointer] {
        if (!weak_this)
            return;
        weak_this->m_child_web_views.remove_first_matching([child_web_view_pointer](auto const& child) {
            return child.ptr() == child_web_view_pointer;
        });
    });
}

void HeadlessWebView::schedule_forced_close()
{
    if (!m_forced_close_timer) {
        m_forced_close_timer = Core::Timer::create_single_shot(AK::Duration::from_milliseconds(child_close_timeout_ms), [weak_this = make_weak_ptr<HeadlessWebView>()] {
            if (!weak_this || !weak_this->is_attached() || weak_this->tab().handle().is_empty() || !weak_this->tab().page().is_open())
                return;
            weak_this->tab().force_close();
        });
    }

    if (!m_forced_close_timer->is_active())
        m_forced_close_timer->start();
}

void HeadlessWebView::reset_viewport_size(Web::DevicePixelSize size)
{
    m_viewport_size = size;

    tab().set_window_size(size.to_type<int>());
    tab().set_viewport_size(m_viewport_size);
}

}
