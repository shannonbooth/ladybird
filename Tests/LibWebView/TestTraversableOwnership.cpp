/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ScopeGuard.h>
#include <LibTest/TestCase.h>
#include <LibURL/Parser.h>
#include <LibWebView/CanonicalBrowsingContext.h>
#include <LibWebView/CanonicalDocument.h>
#include <LibWebView/CanonicalTraversable.h>
#include <LibWebView/UserAgentTopLevelTraversableSet.h>

static WebView::CanonicalTraversable& create_traversable(NonnullRefPtr<WebView::BrowsingSession> session, u64 id)
{
    return WebView::CanonicalTraversable::create_a_new_top_level_traversable(move(session), { 100, id }, {}, Web::HTML::create_initial_session_history_entry_descriptor({ 101, id }, {}, {}), { 102, id });
}

TEST_CASE(unattached_traversable_is_owned_and_found_by_the_user_agent)
{
    auto& traversable = create_traversable(WebView::BrowsingSession::create(WebView::IsPrivate::No), 1);
    ScopeGuard destroy = [&] { traversable.destroy(); };
    EXPECT(!traversable.view().has_value());
    auto& set = WebView::UserAgentTopLevelTraversableSet::the();
    EXPECT_EQ(set.find(traversable.tab_id()), &traversable);
    EXPECT_EQ(set.find_by_handle(traversable.handle()), &traversable);
    EXPECT_EQ(WebView::CanonicalTraversable::traversable_containing(traversable.id()), &traversable);
    EXPECT_EQ(WebView::CanonicalTraversable::navigable_with_active_browsing_context(traversable.active_browsing_context()), &traversable);
    EXPECT(WebView::CanonicalTraversable::is_origin_held_by_a_document(traversable.active_document().origin()));
}

TEST_CASE(traversable_destruction_removes_every_lookup_and_releases_its_session)
{
    auto session = WebView::BrowsingSession::create(WebView::IsPrivate::Yes);
    auto weak_session = session->make_weak_ptr();
    auto& traversable = create_traversable(move(session), 2);
    auto id = traversable.id();
    auto tab_id = traversable.tab_id();
    auto handle = traversable.handle();
    EXPECT(weak_session);
    EXPECT_EQ(&traversable.session(), weak_session.ptr());
    traversable.destroy();
    auto& set = WebView::UserAgentTopLevelTraversableSet::the();
    EXPECT(!set.find(tab_id));
    EXPECT(!set.find_by_handle(handle));
    EXPECT(!WebView::CanonicalTraversable::traversable_containing(id));
    EXPECT(!weak_session);
}

TEST_CASE(tabs_have_distinct_identity_even_when_they_share_a_session)
{
    auto session = WebView::BrowsingSession::create(WebView::IsPrivate::No);
    auto& first = create_traversable(session, 3);
    ScopeGuard destroy_first = [&] { first.destroy(); };
    auto& second = create_traversable(session, 4);
    ScopeGuard destroy_second = [&] { second.destroy(); };
    EXPECT_NE(first.tab_id(), second.tab_id());
    EXPECT_NE(first.handle(), second.handle());
    EXPECT_EQ(&first.session(), &second.session());
    EXPECT(!first.handle().is_empty());
    Vector<WebView::TabId> order;
    WebView::UserAgentTopLevelTraversableSet::the().for_each([&](auto& traversable) {
        order.append(traversable.tab_id());
        return IterationDecision::Continue;
    });
    EXPECT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], first.tab_id());
    EXPECT_EQ(order[1], second.tab_id());
}

TEST_CASE(presentation_is_retained_without_a_view_and_observers_can_disconnect)
{
    auto& traversable = create_traversable(WebView::BrowsingSession::create(WebView::IsPrivate::No), 5);
    ScopeGuard destroy = [&] { traversable.destroy(); };
    auto& state = traversable.presentation();
    auto url = URL::Parser::basic_parse("https://example.com/"sv).release_value();
    size_t notifications = 0;
    state.on_change = [&](auto) { ++notifications; };
    state.set_title("Document title"_utf16);
    state.set_url(url);
    state.set_loading(true);
    EXPECT_EQ(notifications, 3u);
    state.on_change = nullptr;
    state.set_title("Changed without a view"_utf16);
    state.set_loading(false);
    EXPECT_EQ(notifications, 3u);
    EXPECT_EQ(state.title(), "Changed without a view"_utf16);
    EXPECT_EQ(state.url(), url);
    EXPECT(!state.is_loading());
}

TEST_CASE(blocked_resource_totals_include_each_hosting_page)
{
    auto& traversable = create_traversable(WebView::BrowsingSession::create(WebView::IsPrivate::No), 6);
    ScopeGuard destroy = [&] { traversable.destroy(); };
    auto& state = traversable.presentation();
    auto url = URL::Parser::basic_parse("https://ads.example/script.js"sv).release_value();
    state.did_block_resource(Web::PageId { 1 }, url, "Script"sv);
    state.did_block_resource(Web::PageId { 1 }, url, "Script"sv);
    state.did_block_resource(Web::PageId { 2 }, url, "unknown kind"sv);
    EXPECT_EQ(state.blocked_resource_count(), 3u);
    EXPECT_EQ(state.blocked_resources().size(), 2u);
    EXPECT_EQ(state.blocked_resources()[1].kind, "Request"sv);
    state.page_closed(Web::PageId { 1 });
    EXPECT_EQ(state.blocked_resource_count(), 1u);
    state.reset_blocked_resources();
    EXPECT_EQ(state.blocked_resource_count(), 0u);
    EXPECT(state.blocked_resources().is_empty());
}
