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
    auto theme = owner.surface() ? owner.surface()->system_theme() : Core::AnonymousBuffer {};
    auto& tab = traversable.view();
    tab.set_owner_view(owner);
    traversable.set_system_visibility_state(Web::HTML::VisibilityState::Visible);
    auto web_view = adopt_own(*new HeadlessWebView(move(theme), size.to_type<Web::DevicePixels>(), tab));
    tab.activate();
    return make<HeadlessPictureInPictureWindow>(size, move(web_view));
}

ViewImplementation& HeadlessWebView::open_tab(IsPrivate is_private)
{
    return ViewImplementation::open_tab(make<ViewImplementation>(is_private), Web::HTML::VisibilityState::Visible);
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
    : m_theme(move(theme))
    , m_viewport_size(viewport_size)
{
    tab.attach_surface(*this);

    // A headless window is exactly its viewport, so every page gets the viewport as its window size.
    tab.set_window_size(viewport_size.to_type<int>());

    tab.on_new_web_view = [this](auto, auto, CanonicalTraversable& traversable) {
        return adopt_child_web_view(create_child(*this, traversable)).tab().handle();
    };

    tab.on_reposition_window = [this](auto position) {
        m_previous_dimensions.set_location(position.template to_type<Web::DevicePixels>());
        this->tab().set_window_position(position);
    };

    tab.on_resize_window = [this](auto size) {
        m_viewport_size = size.template to_type<Web::DevicePixels>();

        this->tab().set_window_size(size);
        this->tab().handle_resize();
    };

    tab.on_restore_window = [this]() {
        this->tab().set_system_visibility_state(Web::HTML::VisibilityState::Visible);
    };

    tab.on_minimize_window = [this]() {
        this->tab().set_system_visibility_state(Web::HTML::VisibilityState::Hidden);
    };

    tab.on_maximize_window = [this]() {
        m_viewport_size = screen_rect.size();
        m_previous_dimensions = screen_rect;

        this->tab().set_window_position(screen_rect.location().to_type<int>());
        this->tab().set_window_size(screen_rect.size().to_type<int>());
        this->tab().handle_resize();
    };

    tab.on_fullscreen_window = [this]() {
        m_previous_dimensions.set_size(m_viewport_size);
        m_viewport_size = screen_rect.size();

        this->tab().set_window_position(screen_rect.location().to_type<int>());
        this->tab().set_window_size(screen_rect.size().to_type<int>());
        this->tab().set_is_fullscreen(Web::ViewportIsFullscreen::Yes);
    };

    tab.on_exit_fullscreen_window = [this]() {
        m_viewport_size = m_previous_dimensions.size();

        this->tab().set_window_position(m_previous_dimensions.location().to_type<int>());
        this->tab().set_window_size(m_previous_dimensions.size().to_type<int>());
        this->tab().set_is_fullscreen(Web::ViewportIsFullscreen::No);
    };

    tab.on_request_alert = [this](auto const&, auto reply) {
        m_pending_dialog_reply = move(reply);
    };

    tab.on_request_confirm = [this](auto const&, auto reply) {
        m_pending_dialog_reply = move(reply);
    };

    tab.on_request_prompt = [this](auto const&, auto const& prompt_text, auto reply) {
        m_pending_dialog_reply = move(reply);
        m_pending_prompt_text = prompt_text;
    };

    tab.on_request_set_prompt_text = [this](auto const& prompt_text) {
        m_pending_prompt_text = prompt_text;
    };

    tab.on_request_accept_dialog = [this]() {
        VERIFY(m_pending_dialog_reply);
        m_pending_dialog_reply.release_nonnull()->complete(true, move(m_pending_prompt_text));
    };

    tab.on_request_dismiss_dialog = [this]() {
        VERIFY(m_pending_dialog_reply);
        m_pending_dialog_reply.release_nonnull()->complete(false, {});
        m_pending_prompt_text.clear();
    };
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

    if (is_attached() && tab().on_web_content_crashed)
        tab().on_web_content_crashed(crash_reason);
}

HeadlessWebView& HeadlessWebView::adopt_child_web_view(NonnullOwnPtr<HeadlessWebView> web_view)
{
    // A window outlives the window that opened it, so the root window owns every window opened from its windows.
    if (m_parent_web_view)
        return m_parent_web_view->adopt_child_web_view(move(web_view));

    auto* child_web_view = web_view.ptr();
    auto weak_this = make_weak_ptr<HeadlessWebView>();
    web_view->m_parent_web_view = weak_this;
    auto discard_child_web_view = [weak_this, child_web_view]() {
        if (weak_this)
            weak_this->discard_child_web_view(*child_web_view);
    };

    // Propagate crashes from child windows to the parent, so parent tests don't hang waiting for a child that crashed.
    web_view->tab().on_web_content_crashed = [child_web_view, discard_child_web_view](auto crash_reason) {
        child_web_view->propagate_web_content_crash(crash_reason);
        discard_child_web_view();
    };
    web_view->tab().on_close = move(discard_child_web_view);

    m_child_web_views.append(move(web_view));
    return *m_child_web_views.last();
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

Optional<ScreenRects> HeadlessWebView::screen_rects() const
{
    return ScreenRects { .rects = { screen_rect }, .available_rects = { screen_rect }, .main_screen_index = 0 };
}

void HeadlessWebView::reset_viewport_size(Web::DevicePixelSize size)
{
    m_viewport_size = size;

    tab().set_window_size(size.to_type<int>());
    tab().handle_resize();
}

}
