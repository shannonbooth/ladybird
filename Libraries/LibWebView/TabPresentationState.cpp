/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/AnyOf.h>
#include <LibCore/Timer.h>
#include <LibGfx/ImageFormats/PNGWriter.h>
#include <LibWebView/Application.h>
#include <LibWebView/BookmarkStore.h>
#include <LibWebView/CanonicalTraversable.h>
#include <LibWebView/FaviconStore.h>
#include <LibWebView/HistoryStore.h>
#include <LibWebView/TabPresentationState.h>
#include <LibWebView/WebContentPage.h>

namespace WebView {

TabPresentationState::TabPresentationState(CanonicalTraversable& traversable)
    : m_traversable(traversable)
{
}

TabPresentationState::~TabPresentationState() = default;

void TabPresentationState::set_title(Utf16String title)
{
    if (m_title == title)
        return;
    m_title = move(title);
    changed(Change::Title);
}

void TabPresentationState::set_url(URL::URL url)
{
    if (m_url == url)
        return;
    m_url = move(url);
    changed(Change::URL);
}

void TabPresentationState::set_favicon(Optional<Gfx::Bitmap const&> favicon)
{
    m_favicon = favicon.has_value() ? &*favicon : nullptr;
    m_favicon_hash.clear();
    auto& session = m_traversable.session();
    if (favicon.has_value()) {
        if (auto png = Gfx::PNGWriter::encode(*favicon); !png.is_error() && session.favicon_store)
            m_favicon_hash = session.favicon_store->add_favicon(png.release_value());
        if (m_favicon_hash.has_value()) {
            if (session.is_private() == IsPrivate::No)
                Application::bookmark_store().update_favicon(m_url, *m_favicon_hash);
            if (session.history_store)
                session.history_store->update_favicon(m_url, *m_favicon_hash);
        }
    }
    changed(Change::Favicon);
}

void TabPresentationState::set_loading(bool loading)
{
    if (m_is_loading == loading)
        return;
    m_is_loading = loading;
    changed(Change::Loading);
}

void TabPresentationState::update_audibility()
{
    auto audible = m_audibility_hold.is_held(MonotonicTime::now());
    m_traversable.for_each_hosting_page([&](WebContentPage& page) {
        audible |= page.audibility() == Audio::Audibility::Audible;
    });
    auto audibility = audible ? Audio::Audibility::Audible : Audio::Audibility::Inaudible;
    if (m_audibility == audibility)
        return;
    m_audibility = audibility;
    ++m_audibility_change_count;
    changed(Change::Audibility);
}

void TabPresentationState::page_left(WebContentPage const& page)
{
    if (page.audibility() != Audio::Audibility::Audible)
        return;
    auto now = MonotonicTime::now();
    if (!m_audibility_hold.page_left(page.id(), now))
        return;
    if (!m_audibility_hold_timer) {
        m_audibility_hold_timer = Core::Timer::create_single_shot(AK::Duration::zero(), [this] {
            auto now = MonotonicTime::now();
            if (m_audibility_hold.is_held(now)) {
                m_audibility_hold_timer->restart(*m_audibility_hold.held_until() - now);
                return;
            }
            update_audibility();
        });
    }
    m_audibility_hold_timer->restart(*m_audibility_hold.held_until() - now);
}

void TabPresentationState::page_closed(Web::PageId id)
{
    m_audibility_hold.page_closed(id);
    if (m_blocked_resource_counts.remove(id))
        changed(Change::ContentBlocking);
}

void TabPresentationState::did_block_resource(Web::PageId page, URL::URL const& url, StringView kind)
{
    ++m_blocked_resource_counts.ensure(page);
    static constexpr Array kinds { "Script"sv, "Image"sv, "Stylesheet"sv, "Font"sv, "Media"sv, "Frame"sv, "Request"sv };
    auto resource_kind = "Request"sv;
    for (auto known_kind : kinds) {
        if (known_kind == kind) {
            resource_kind = known_kind;
            break;
        }
    }
    auto host = MUST(String::from_utf8(url.serialized_host()));
    if (!host.is_empty() && m_blocked_resources.size() < 100
        && !any_of(m_blocked_resources, [&](auto const& resource) { return resource.host == host && resource.kind == resource_kind; }))
        m_blocked_resources.append({ move(host), resource_kind });
    changed(Change::ContentBlocking);
}

u64 TabPresentationState::blocked_resource_count() const
{
    u64 count = 0;
    for (auto const& entry : m_blocked_resource_counts)
        count += entry.value;
    return count;
}

void TabPresentationState::reset_blocked_resources()
{
    m_blocked_resources.clear();
    m_blocked_resource_counts.clear();
    changed(Change::ContentBlocking);
}

bool TabPresentationState::hosts_top_level_document(WebContentPage const& page) const
{
    return m_traversable.display_page() == &page;
}

void TabPresentationState::set_screen_wake_lock_state(Web::ScreenWakeLockState state)
{
    if (m_screen_wake_lock_state == state)
        return;
    m_screen_wake_lock_state = state;
    changed(Change::ScreenWakeLock);
}

void TabPresentationState::set_text_fragment_indication_visible(bool visible)
{
    if (m_text_fragment_indication_is_visible == visible)
        return;
    m_text_fragment_indication_is_visible = visible;
    changed(Change::TextFragmentIndication);
}

void TabPresentationState::did_change_theme_color(WebContentPage const& page, Gfx::Color color)
{
    if (!hosts_top_level_document(page) || m_theme_color == color)
        return;
    m_theme_color = color;
    changed(Change::ThemeColor);
}

void TabPresentationState::did_change_background_color(WebContentPage const& page, Gfx::Color color)
{
    if (!hosts_top_level_document(page) || m_background_color == color)
        return;
    m_background_color = color;
    changed(Change::BackgroundColor);
}

void TabPresentationState::did_update_resource_count(WebContentPage const& page, i32 count_waiting)
{
    if (!hosts_top_level_document(page) || m_resource_count == count_waiting)
        return;
    m_resource_count = count_waiting;
    changed(Change::ResourceCount);
}

void TabPresentationState::did_change_screen_wake_lock_state(WebContentPage const& page, Web::ScreenWakeLockState state)
{
    if (hosts_top_level_document(page))
        set_screen_wake_lock_state(state);
}

void TabPresentationState::did_change_text_fragment_indication_visibility(WebContentPage const& page, bool visible)
{
    if (hosts_top_level_document(page))
        set_text_fragment_indication_visible(visible);
}

}
