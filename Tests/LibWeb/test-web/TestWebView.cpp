/*
 * Copyright (c) 2024-2025, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "TestWebView.h"

#include "Application.h"

#include <LibCore/AnonymousBuffer.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/ShareableBitmap.h>
#include <LibWebView/NotificationManager.h>
#include <LibWebView/PermissionManager.h>
#include <LibWebView/RecordingNotificationBackend.h>
#include <LibWebView/WebContentPage.h>

namespace TestWeb {

NonnullOwnPtr<TestWebView> TestWebView::create(Core::AnonymousBuffer theme, Web::DevicePixelSize window_size)
{
    auto view = adopt_own(*new TestWebView(move(theme), window_size, open_tab(WebView::IsPrivate::No)));
    view->tab().activate();
    return view;
}

TestWebView::TestWebView(Core::AnonymousBuffer theme, Web::DevicePixelSize viewport_size, WebView::ViewImplementation& tab)
    : WebView::HeadlessWebView(move(theme), viewport_size, tab)
    , m_test_promise(TestPromise::construct())
{
}

void TestWebView::clear_content_blockers()
{
    tab().page().client().async_set_content_blockers(MUST(Core::AnonymousBuffer::create_with_size(0)));
}

// Force-dark rides on the navigable, so a test that turns it on leaves it on for whatever runs next in this view.
void TestWebView::reset_force_dark()
{
    tab().debug_request("set-force-dark"sv, "off"sv);
}

// Same story as force-dark above: the flag lives on the navigable and would otherwise outlive the test that set it.
void TestWebView::reset_line_box_borders()
{
    tab().debug_request("set-line-box-borders"sv, "off"sv);
}

// The user style lives on the page too, so one that a test sets would otherwise style every test after it in this view.
void TestWebView::reset_user_style()
{
    tab().debug_request("clear-user-style"sv);
}

// The emulated position lives on the page, so a test that moves it would otherwise hand its position to the next test.
void TestWebView::reset_geolocation_emulated_position()
{
    tab().geolocation_settings_changed();
}

// Permissions live in the UI process, and outlive the test that changed them. Each test has an origin of its own, and
// tests run concurrently, so only this test's origin is reset.
void TestWebView::reset_permissions()
{
    tab().session().permission_manager->reset_origin(tab().url().origin());
    tab().reset_permission_prompts_for_testing();
}

// A test's notifications live in the UI process, keyed by a page id the next test in this view gets too, so they may
// not reach the next test.
void TestWebView::reset_notifications()
{
    WebView::NotificationManager::the().reset_page_for_testing(tab().page());
    if (auto* recording_backend = WebView::NotificationManager::the().backend().as_recording_backend())
        recording_backend->forget_page(tab().page().id());
}

NonnullRefPtr<Core::Promise<Empty>> TestWebView::reset_state_for_next_test()
{
    return tab().reset_state_for_testing();
}

NonnullRefPtr<Core::Promise<RefPtr<Gfx::Bitmap const>>> TestWebView::take_screenshot()
{
    VERIFY(!m_pending_screenshot);

    m_pending_screenshot = Core::Promise<RefPtr<Gfx::Bitmap const>>::construct();
    auto reply = WebView::ScreenshotReply::create([weak_this = make_weak_ptr<TestWebView>()](ErrorOr<Gfx::ShareableBitmap> screenshot) {
        // NOTE: The screenshot may arrive after a timeout already completed the test and cleared m_pending_screenshot.
        auto* view = weak_this.ptr();
        if (!view || !view->m_pending_screenshot || screenshot.is_error())
            return;

        auto pending_screenshot = move(view->m_pending_screenshot);
        pending_screenshot->resolve(screenshot.value().bitmap());
    },
        Error::from_string_literal("The page closed before taking the screenshot"));
    tab().page().async_take_document_screenshot(tab().page().expect_reply_from_renderer(move(reply)));

    return *m_pending_screenshot;
}

void TestWebView::on_test_complete(TestCompletion completion)
{
    m_pending_screenshot.clear();
    if (auto reply = move(m_pending_dialog_reply))
        reply->cancel();
    m_pending_prompt_text.clear();
    tab().reset_viewport_for_testing();

    m_test_promise->resolve(move(completion));
}

}
