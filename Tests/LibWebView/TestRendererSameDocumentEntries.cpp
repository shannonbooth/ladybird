/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "HeadlessTest.h"
#include <AK/LexicalPath.h>
#include <AK/Random.h>
#include <LibCore/EventLoop.h>
#include <LibCore/Timer.h>
#include <LibGfx/SystemTheme.h>
#include <LibTest/TestCase.h>
#include <LibURL/Parser.h>
#include <LibWebCommon/HTML/HistoryOperation.h>
#include <LibWebView/Application.h>
#include <LibWebView/CanonicalDocument.h>
#include <LibWebView/CanonicalTraversable.h>
#include <LibWebView/HeadlessWebView.h>
#include <LibWebView/Utilities.h>
#include <LibWebView/WebContentClient.h>
#include <LibWebView/WebContentPage.h>

namespace {

class TestApplication : public Test::HeadlessApplication {
    WEB_VIEW_APPLICATION(TestApplication)

public:
    explicit TestApplication(Optional<ByteString> ladybird_binary_path)
        : Test::HeadlessApplication(move(ladybird_binary_path))
    {
    }

    virtual void create_platform_options(WebView::BrowserOptions& browser_options, WebView::RequestServerOptions&, WebView::WebContentOptions& web_content_options) override
    {
        browser_options.headless_mode = WebView::HeadlessMode::Test;
        browser_options.disable_sql_database = WebView::DisableSQLDatabase::Yes;
        web_content_options.is_test_mode = WebView::IsTestMode::No;
    }
};

URL::URL parse(StringView url)
{
    return URL::Parser::basic_parse(url).release_value();
}

// Reports a same-document entry for the view's active document, as the process hosting it does after running the
// given operation, and returns whether the UI process accepted it rather than terminating that process.
bool report_same_document_entry(WebView::HeadlessWebView& view, Web::HTML::SameDocumentEntrySource source, URL::URL url, Web::HTML::HistoryHandlingBehavior history_handling, Optional<Web::HTML::CrossProcessId> entry_document_id = {})
{
    NonnullRefPtr<WebView::WebContentClient> client = view.page().client();
    auto& stub = static_cast<WebContentClientStub&>(*client);
    auto& traversable = view.traversable();
    auto entry_to_replace = traversable.active_session_history_entry()->identity();

    auto uuid = generate_random_uuid();
    Web::HTML::SameDocumentNavigationEntry entry;
    entry.url = move(url);
    entry.document_state_id = traversable.active_session_history_entry()->document_state->id;
    entry.navigation_api_id = Utf16String::from_utf8_without_validation(uuid.bytes());

    Web::FinalizeSameDocumentNavigationHistoryOperationParameters parameters {
        .navigable_id = traversable.id(),
        .target_entry = move(entry),
        .entry_to_replace = {},
        .previous_entry_persisted_state = {},
        .history_handling = history_handling,
        .user_involvement = Web::HTML::UserNavigationInvolvement::None,
        .source = source,
        .entry_document_id = entry_document_id,
    };
    if (history_handling == Web::HTML::HistoryHandlingBehavior::Replace)
        parameters.entry_to_replace = entry_to_replace;

    Web::HTML::CrossProcessId operation_id { .namespace_id = get_random<u64>() | 1, .local_id = 1 };
    stub.request_history_operation(view.page().id(), operation_id, move(parameters));
    return client->is_open();
}

}

// A same-document entry gets its URL from the URL and history update steps, or from navigating to a fragment, and the
// operation that ran them limits it. The UI process holds the document's URL and origin, so it refuses an entry that
// operation could not have created for the document, and the process that reported it.

TEST_CASE(renderer_same_document_entries)
{
    Test::HeadlessTestEnvironment environment { "TestRendererSameDocumentEntries"sv };

    auto app = TRY_OR_FAIL(environment.create_application<TestApplication>());

    auto theme_path = LexicalPath::join(WebView::s_ladybird_resource_root, "themes"sv, "Default.ini"sv);
    auto theme = TRY_OR_FAIL(Gfx::load_system_theme(theme_path.string()));

    {
        auto view = WebView::HeadlessWebView::create(theme, { 800, 600 });
        auto& document = view->traversable().active_document();
        VERIFY(document.is_initial_about_blank());

        // A fragment is a URL the document can have its URL rewritten to, and the reported entry becomes the
        // navigable's active entry as the document's URL changes.
        VERIFY(report_same_document_entry(*view, Web::HTML::SameDocumentEntrySource::HistoryAPI, parse("about:blank#fragment"sv), Web::HTML::HistoryHandlingBehavior::Replace));
        VERIFY(document.url() == parse("about:blank#fragment"sv));
        VERIFY(view->traversable().active_session_history_entry()->url == parse("about:blank#fragment"sv));

        // pushState() can't change an about: URL's query, but a new window's initial about:blank takes any URL that
        // matches about:blank.
        VERIFY(!report_same_document_entry(*WebView::HeadlessWebView::create(theme, { 800, 600 }), Web::HTML::SameDocumentEntrySource::HistoryAPI, parse("about:blank?query"sv), Web::HTML::HistoryHandlingBehavior::Replace));
        VERIFY(report_same_document_entry(*view, Web::HTML::SameDocumentEntrySource::AboutBlank, parse("about:blank?query"sv), Web::HTML::HistoryHandlingBehavior::Replace));
        VERIFY(document.url() == parse("about:blank?query"sv));

        // document.open() ends the initial about:blank, whose entries can then be pushed.
        static_cast<WebContentClientStub&>(view->page().client()).did_end_initial_about_blank(view->page().id(), document.id());
        VERIFY(!document.is_initial_about_blank());
        VERIFY(report_same_document_entry(*view, Web::HTML::SameDocumentEntrySource::FragmentNavigation, parse("about:blank?query#pushed"sv), Web::HTML::HistoryHandlingBehavior::Push));
    }

    auto refuses = [&](Web::HTML::SameDocumentEntrySource source, StringView url, Web::HTML::HistoryHandlingBehavior history_handling, Optional<Web::HTML::CrossProcessId> entry_document_id = {}) {
        auto view = WebView::HeadlessWebView::create(theme, { 800, 600 });
        return !report_same_document_entry(*view, source, parse(url), history_handling, entry_document_id);
    };

    // A local file, or a document of another origin.
    VERIFY(refuses(Web::HTML::SameDocumentEntrySource::HistoryAPI, "file:///etc/passwd"sv, Web::HTML::HistoryHandlingBehavior::Replace));
    VERIFY(refuses(Web::HTML::SameDocumentEntrySource::NavigationAPIIntercept, "https://victim.example/"sv, Web::HTML::HistoryHandlingBehavior::Replace));

    // A fragment navigation needs a fragment.
    VERIFY(refuses(Web::HTML::SameDocumentEntrySource::FragmentNavigation, "about:blank"sv, Web::HTML::HistoryHandlingBehavior::Replace));

    // document.open() from an entry document the process does not host, or of document itself to another URL.
    VERIFY(refuses(Web::HTML::SameDocumentEntrySource::DocumentOpen, "https://victim.example/"sv, Web::HTML::HistoryHandlingBehavior::Replace, Web::HTML::CrossProcessId { .namespace_id = 1, .local_id = 1 }));
    VERIFY(refuses(Web::HTML::SameDocumentEntrySource::DocumentOpen, "about:blank#other"sv, Web::HTML::HistoryHandlingBehavior::Replace));

    // A push, as an initial about:blank's entries replace its active one.
    VERIFY(refuses(Web::HTML::SameDocumentEntrySource::HistoryAPI, "about:blank#fragment"sv, Web::HTML::HistoryHandlingBehavior::Push));

    outln("PASS: renderers cannot give a same-document entry a URL its document cannot take");
}

// A popstate handler that pushes state during a same-document traversal makes its entry active before the process
// acknowledges the traversal. The UI process keeps that entry active rather than the traversal's.
TEST_CASE(entry_pushed_during_a_traversal_stays_active)
{
    Test::HeadlessTestEnvironment environment { "TestRendererSameDocumentEntriesTraversal"sv };

    auto app = TRY_OR_FAIL(environment.create_application<TestApplication>());

    auto theme_path = LexicalPath::join(WebView::s_ladybird_resource_root, "themes"sv, "Default.ini"sv);
    auto theme = TRY_OR_FAIL(Gfx::load_system_theme(theme_path.string()));

    auto view = WebView::HeadlessWebView::create(theme, { 800, 600 });
    size_t loads_finished = 0;
    view->on_load_finish = [&](auto const&) { ++loads_finished; };
    Utf16String title;
    view->on_title_change = [&](auto const& new_title) { title = new_title; };

    auto timed_out = false;
    auto timeout = Core::Timer::create_single_shot(AK::Duration::from_seconds(10), [&] { timed_out = true; });
    timeout->start();
    auto wait_until = [&](Function<bool()> condition) {
        Core::EventLoop::current().spin_until([&] { return timed_out || condition(); });
        VERIFY(!timed_out);
    };

    // Wait out the initial about:blank load; navigating before it completes would drop the navigation.
    wait_until([&] { return loads_finished >= 1; });

    // The title is set from a later task than the popstate handler, so it arrives after the traversal's acknowledgement.
    // In the data: URL, %23 stands for a # in the script.
    auto url = parse(R"(data:text/html,<script>addEventListener("popstate", () => { history.pushState(null, "", "%23during"); setTimeout(() => { document.title = "settled"; }); }, { once: true });</script>)"sv);
    view->load_from_browser_ui(url);
    wait_until([&] { return loads_finished >= 2; });

    view->run_javascript("history.pushState(null, '', '#pushed')"_string);
    wait_until([&] { return view->url().fragment() == "pushed"sv; });

    view->run_javascript("history.back()"_string);
    wait_until([&] { return title == u"settled"sv; });

    VERIFY(view->url().fragment() == "during"sv);
    VERIFY(view->traversable().active_session_history_entry()->url.fragment() == "during"sv);

    outln("PASS: an entry pushed during a same-document traversal stays the active entry");
}
