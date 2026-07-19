/*
 * Copyright (c) 2024, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/HTML/LocalNavigable.h>
#include <LibWeb/HTML/SessionHistoryTraversalQueue.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(SessionHistoryTraversalQueue);
GC_DEFINE_ALLOCATOR(SessionHistoryTraversalQueueEntry);

GC::Ref<SessionHistoryTraversalQueueEntry> SessionHistoryTraversalQueueEntry::create(JS::VM& vm, GC::Ref<SessionHistoryTraversalSteps> steps, GC::Ptr<HTML::LocalNavigable> target_navigable)
{
    return vm.heap().allocate<SessionHistoryTraversalQueueEntry>(steps, target_navigable);
}

void SessionHistoryTraversalQueueEntry::visit_edges(JS::Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_steps);
    visitor.visit(m_target_navigable);
}

SessionHistoryTraversalQueue::SessionHistoryTraversalQueue() = default;

void SessionHistoryTraversalQueue::process_queue()
{
    while (m_queue.size() > 0) {
        if (m_current_promise && !m_current_promise->is_resolved() && !m_current_promise->is_rejected()) {
            m_current_promise->when_resolved([this](Empty) {
                process_queue();
            });
            return;
        }

        auto entry = m_queue.take_first();
        m_current_promise = Core::Promise<Empty>::construct();
        entry->execute_steps(*m_current_promise);
    }
}

void SessionHistoryTraversalQueue::visit_edges(JS::Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_queue);
}

void SessionHistoryTraversalQueue::append(GC::Ref<SessionHistoryTraversalSteps> steps)
{
    m_queue.append(SessionHistoryTraversalQueueEntry::create(vm(), steps, nullptr));
    schedule_processing();
}

void SessionHistoryTraversalQueue::append_sync(GC::Ref<SessionHistoryTraversalSteps> steps, GC::Ptr<LocalNavigable> target_navigable)
{
    m_queue.append(SessionHistoryTraversalQueueEntry::create(vm(), steps, target_navigable));
    schedule_processing();
}

void SessionHistoryTraversalQueue::schedule_processing()
{
    if (!m_processing_scheduled) {
        m_processing_scheduled = true;
        Core::deferred_invoke([this] {
            m_processing_scheduled = false;
            process_queue();
        });
    }
}

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#sync-navigations-jump-queue
}
