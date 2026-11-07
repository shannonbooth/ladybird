/*
 * Copyright (c) 2025, Ben Eidson <b.e.eidson@gmail.com>
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ptr;
use std::sync::atomic::{AtomicPtr, Ordering};

use super::control_message::ControlMessage;

struct Entry {
    message: Option<ControlMessage>,
    next: *mut Entry,
}

// https://webaudio.github.io/web-audio-api/#control-message-queue
#[derive(Default)]
pub struct ControlMessageQueue {
    pending: AtomicPtr<Entry>,
    retired: AtomicPtr<Entry>,
}

impl ControlMessageQueue {
    // The control thread owns allocation and reclamation of queue entries.
    pub fn enqueue(&self, message: ControlMessage) {
        unsafe { drop_entries(self.retired.swap(ptr::null_mut(), Ordering::AcqRel)) };
        let entry = Box::into_raw(Box::new(Entry {
            message: Some(message),
            next: ptr::null_mut(),
        }));
        unsafe { publish_entries(&self.pending, entry, entry) };
    }

    pub fn drain(&self) -> ControlMessageBatch<'_> {
        let last = self.pending.swap(ptr::null_mut(), Ordering::AcqRel);
        let mut entry = last;
        let mut first = ptr::null_mut();
        while !entry.is_null() {
            // SAFETY: The swap transferred this list to the rendering thread.
            let next = unsafe { (*entry).next };
            unsafe { (*entry).next = first };
            first = entry;
            entry = next;
        }
        ControlMessageBatch {
            first,
            last,
            next: first,
            retired: &self.retired,
        }
    }
}

impl Drop for ControlMessageQueue {
    fn drop(&mut self) {
        unsafe {
            drop_entries(*self.pending.get_mut());
            drop_entries(*self.retired.get_mut());
        }
    }
}

pub struct ControlMessageBatch<'a> {
    first: *mut Entry,
    last: *mut Entry,
    next: *mut Entry,
    retired: &'a AtomicPtr<Entry>,
}

impl Iterator for ControlMessageBatch<'_> {
    type Item = ControlMessage;

    fn next(&mut self) -> Option<Self::Item> {
        let entry = unsafe { self.next.as_mut() }?;
        self.next = entry.next;
        entry.message.take()
    }
}

impl Drop for ControlMessageBatch<'_> {
    fn drop(&mut self) {
        if !self.first.is_null() {
            unsafe { publish_entries(self.retired, self.first, self.last) };
        }
    }
}

// SAFETY: The caller owns a nonempty list starting at first and ending at last.
unsafe fn publish_entries(head: &AtomicPtr<Entry>, first: *mut Entry, last: *mut Entry) {
    let mut old_head = head.load(Ordering::Relaxed);
    loop {
        unsafe { (*last).next = old_head };
        match head.compare_exchange_weak(old_head, first, Ordering::Release, Ordering::Relaxed) {
            Ok(_) => return,
            Err(head) => old_head = head,
        }
    }
}

// SAFETY: The caller owns every entry reachable from entry.
unsafe fn drop_entries(mut entry: *mut Entry) {
    while !entry.is_null() {
        let owned_entry = unsafe { Box::from_raw(entry) };
        entry = owned_entry.next;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::css::style::pipeline::allocation_count::count_allocations;
    use crate::web_audio::control_message::RemoveNode;

    #[test]
    fn drains_one_batch_in_order_without_allocating() {
        let queue = ControlMessageQueue::default();
        for node_id in 0..64 {
            queue.enqueue(ControlMessage::RemoveNode(RemoveNode { node_id }));
        }

        let (_, allocations) = count_allocations(|| {
            let mut batch = queue.drain();
            for expected_node_id in 0..64 {
                let Some(ControlMessage::RemoveNode(message)) = batch.next() else {
                    panic!("expected a node removal");
                };
                assert_eq!(message.node_id, expected_node_id);
            }
            assert!(batch.next().is_none());
        });
        assert_eq!(allocations.everywhere, 0);
        assert!(queue.drain().next().is_none());
    }
}
