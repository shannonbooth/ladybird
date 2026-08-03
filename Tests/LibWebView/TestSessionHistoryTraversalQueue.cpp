/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/EventLoop.h>
#include <LibTest/TestCase.h>
#include <LibWebView/SessionHistoryTraversalQueue.h>

static Web::HTML::CrossProcessId navigable_id(u64 local_id)
{
    return { 1, local_id };
}

static void run_event_loop(Core::EventLoop& event_loop)
{
    Core::deferred_invoke([&event_loop] { event_loop.quit(0); });
    event_loop.exec();
}

TEST_CASE(runs_appended_steps_in_order)
{
    Core::EventLoop event_loop;
    WebView::SessionHistoryTraversalQueue queue;

    Vector<int> ran;
    for (int i = 0; i < 3; ++i) {
        queue.append_session_history_traversal_steps([&ran, i](NonnullRefPtr<Core::Promise<Empty>> signal) {
            ran.append(i);
            signal->resolve({});
        });
    }

    EXPECT(!queue.is_empty());
    run_event_loop(event_loop);

    EXPECT(queue.is_empty());
    EXPECT_EQ(ran, (Vector<int> { 0, 1, 2 }));
}

TEST_CASE(waits_for_running_steps_to_signal_completion)
{
    Core::EventLoop event_loop;
    WebView::SessionHistoryTraversalQueue queue;

    RefPtr<Core::Promise<Empty>> first_signal;
    auto second_ran = false;

    queue.append_session_history_traversal_steps([&first_signal](NonnullRefPtr<Core::Promise<Empty>> signal) {
        first_signal = signal;
    });
    queue.append_session_history_traversal_steps([&second_ran](NonnullRefPtr<Core::Promise<Empty>> signal) {
        second_ran = true;
        signal->resolve({});
    });

    run_event_loop(event_loop);
    EXPECT(first_signal);
    EXPECT(!second_ran);

    first_signal->resolve({});
    run_event_loop(event_loop);
    EXPECT(second_ran);
}

TEST_CASE(takes_only_synchronous_navigation_steps)
{
    Core::EventLoop event_loop;
    WebView::SessionHistoryTraversalQueue queue;

    queue.append_session_history_traversal_steps([](NonnullRefPtr<Core::Promise<Empty>>) { });
    queue.append_session_history_synchronous_navigation_steps(navigable_id(7), [](NonnullRefPtr<Core::Promise<Empty>>) { });

    auto taken = queue.take_first_synchronous_navigation_steps_not_targeting({});
    EXPECT(taken.has_value());
    EXPECT_EQ(taken->target_navigable, navigable_id(7));

    EXPECT(!queue.take_first_synchronous_navigation_steps_not_targeting({}).has_value());
    EXPECT(!queue.is_empty());
}

TEST_CASE(skips_synchronous_navigation_steps_for_excluded_navigables)
{
    Core::EventLoop event_loop;
    WebView::SessionHistoryTraversalQueue queue;

    queue.append_session_history_synchronous_navigation_steps(navigable_id(1), [](NonnullRefPtr<Core::Promise<Empty>>) { });
    queue.append_session_history_synchronous_navigation_steps(navigable_id(2), [](NonnullRefPtr<Core::Promise<Empty>>) { });

    HashTable<Web::HTML::CrossProcessId> excluded;
    excluded.set(navigable_id(1));

    auto taken = queue.take_first_synchronous_navigation_steps_not_targeting(excluded);
    EXPECT(taken.has_value());
    EXPECT_EQ(taken->target_navigable, navigable_id(2));

    EXPECT(!queue.take_first_synchronous_navigation_steps_not_targeting(excluded).has_value());
}
