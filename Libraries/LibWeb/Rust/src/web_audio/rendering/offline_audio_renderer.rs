/*
 * Copyright (c) 2026, Jelle Raaijmakers <jelle@ladybird.org>
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#![allow(clippy::needless_range_loop, clippy::manual_memcpy)]

use std::cell::UnsafeCell;
use std::collections::{HashSet, VecDeque};
use std::rc::Rc;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex};

use super::render_graph::RenderGraph;
use super::render_node::RenderContext;
use crate::web_audio::control_message_queue::ControlMessageQueue;
use crate::web_audio::control_thread::{ControlThread, ControlThreadCell};

pub trait OfflineAudioRendererClient {
    fn rendering_suspended(&self, suspend_time: f64);
    fn rendering_completed(&self, end_frame: u64, rendered_channels: &[Vec<f32>]);
    fn sources_ended(&self, nodes: &[u64]);
}

struct RenderJob {
    end_frame: u64,
    channels: Vec<Vec<f32>>,
}

#[derive(Default)]
struct RenderingState {
    jobs: VecDeque<RenderJob>,
    suspend_frames: HashSet<u64>,

    // The next render quantum boundary at which the rendering thread will look for a scheduled suspension. It is
    // published under the same lock that guards the suspension set, so a suspension can never be registered for a
    // boundary the rendering thread has already checked.
    next_suspend_check_frame: u64,
    suspended: bool,
    shutting_down: bool,
}

// The offline render thread retains the graph between render jobs.
// https://webaudio.github.io/web-audio-api/#OfflineAudioContext
pub struct OfflineAudioRenderer {
    control_message_queue: Arc<ControlMessageQueue>,
    destination_node_id: u64,
    number_of_channels: usize,
    sample_rate: f32,
    quantum_size: usize,
    control_thread: ControlThread,
    client: ControlThreadCell<Option<Rc<dyn OfflineAudioRendererClient>>>,
    thread: UnsafeCell<Option<std::thread::Thread>>,
    graph: UnsafeCell<RenderGraph>,
    frames_rendered: AtomicU64,
    rendering_state: Mutex<RenderingState>,
    rendering_state_changed: Condvar,
}

// SAFETY: One render thread owns graph/sample mutations, and suspension/progress use the original synchronization.
unsafe impl Send for OfflineAudioRenderer {}
unsafe impl Sync for OfflineAudioRenderer {}
enum Notification {
    Complete(RenderJob),
    Suspended(f64),
    SourcesEnded(Vec<u64>),
}

impl OfflineAudioRenderer {
    pub fn new(
        control_message_queue: Arc<ControlMessageQueue>,
        destination_node_id: u64,
        number_of_channels: usize,
        sample_rate: f32,
        quantum_size: usize,
    ) -> Arc<Self> {
        Arc::new(Self {
            control_message_queue,
            destination_node_id,
            number_of_channels,
            sample_rate,
            quantum_size,
            control_thread: ControlThread::current(),
            client: ControlThreadCell::new(None),
            thread: UnsafeCell::new(None),
            graph: UnsafeCell::new(RenderGraph::default()),
            frames_rendered: AtomicU64::new(0),
            rendering_state: Mutex::new(RenderingState::default()),
            rendering_state_changed: Condvar::new(),
        })
    }

    /// The callbacks below are set on the control thread before rendering starts and are always invoked on the control
    /// thread. They are cleared once rendering completes to break the reference cycle with the context.
    pub fn set_client(&self, client: Option<Rc<dyn OfflineAudioRendererClient>>) {
        self.client.replace(client);
    }

    pub fn start_rendering(self: &Arc<Self>) {
        assert!(unsafe { &*self.thread.get() }.is_none());
        unsafe { &mut *self.graph.get() }.set_destination(self.destination_node_id);
        let renderer = Arc::clone(self);
        let thread = std::thread::Builder::new()
            .name("OfflineAudioRenderer".into())
            .spawn(move || {
                renderer.render_thread_main();
            })
            .unwrap();
        unsafe {
            *self.thread.get() = Some(thread.thread().clone());
        }
        drop(thread);
    }

    pub fn enqueue_rendering(&self, end_frame: u64, length: usize) {
        let channels = (0..self.number_of_channels).map(|_| vec![0.; length]).collect();
        let mut state = self.rendering_state.lock().unwrap();
        state.jobs.push_back(RenderJob { end_frame, channels });
        self.rendering_state_changed.notify_one();
    }

    /// Ends rendering without completing it, unblocking the rendering thread if it is waiting in a suspension.
    pub fn stop(&self) {
        let mut state = self.rendering_state.lock().unwrap();
        state.shutting_down = true;
        state.jobs.clear();
        self.rendering_state_changed.notify_all();
    }

    /// Registers a suspension at the given render quantum boundary. Returns false if the rendering thread already
    /// looked for suspensions at that boundary.
    pub fn request_suspend(&self, frame: u64) -> bool {
        let mut state = self.rendering_state.lock().unwrap();
        if frame < self.frames_rendered() || frame < state.next_suspend_check_frame {
            return false;
        }
        state.suspend_frames.insert(frame);
        true
    }

    pub fn resume(&self) {
        let mut state = self.rendering_state.lock().unwrap();
        state.suspended = false;
        self.rendering_state_changed.notify_all();
    }

    pub fn frames_rendered(&self) -> u64 {
        self.frames_rendered.load(Ordering::SeqCst)
    }

    fn post_to_control_thread(self: &Arc<Self>, notification: Notification) {
        let renderer = Arc::clone(self);
        self.control_thread.post(move || {
            let Some(client) = renderer.client.get() else {
                return;
            };
            match notification {
                Notification::Complete(job) => client.rendering_completed(job.end_frame, &job.channels),
                Notification::Suspended(time) => client.rendering_suspended(time),
                Notification::SourcesEnded(nodes) => client.sources_ended(&nodes),
            }
        });
    }

    fn render_thread_main(self: &Arc<Self>) {
        let mut current_frame = 0;
        loop {
            let mut job = {
                let state = self.rendering_state.lock().unwrap();
                let mut state = self
                    .rendering_state_changed
                    .wait_while(state, |state| state.jobs.is_empty() && !state.shutting_down)
                    .unwrap();
                if state.shutting_down {
                    return;
                }
                state.jobs.pop_front().unwrap()
            };
            let first_frame = current_frame;

            // 1. Given the current connections and scheduled changes, start rendering buffer’s length sample-frames of audio into buffer.
            while current_frame < job.end_frame {
                // 2. For every render quantum, check and suspend rendering if necessary.
                {
                    let mut state = self.rendering_state.lock().unwrap();
                    if state.shutting_down {
                        return;
                    }
                    state.next_suspend_check_frame = current_frame + self.quantum_size as u64;
                    if state.suspend_frames.remove(&current_frame) {
                        state.suspended = true;
                        let suspend_time = current_frame as f64 / f64::from(self.sample_rate);
                        self.post_to_control_thread(Notification::Suspended(suspend_time));

                        // 3. If a suspended context is resumed, continue to render the buffer.
                        state = self
                            .rendering_state_changed
                            .wait_while(state, |state| state.suspended && !state.shutting_down)
                            .unwrap();
                        if state.shutting_down {
                            return;
                        }
                    }
                }
                let graph = unsafe { &mut *self.graph.get() };
                graph.apply_control_messages(self.control_message_queue.drain());
                let context = RenderContext {
                    sample_rate: f64::from(self.sample_rate),
                    quantum_size: self.quantum_size,
                    quantum_start_frame: current_frame,
                };
                graph.render_quantum(&context);

                if let Some(destination_node) = graph.destination() {
                    let destination_output = unsafe { &*destination_node.data().output(0) };
                    let offset = (current_frame - first_frame) as usize;
                    for (channel, samples) in job
                        .channels
                        .iter_mut()
                        .enumerate()
                        .take(destination_output.channel_count())
                    {
                        let frames_to_copy = self.quantum_size.min(samples.len() - offset);
                        samples[offset..offset + frames_to_copy]
                            .copy_from_slice(&destination_output.channel(channel)[..frames_to_copy]);
                    }
                }
                current_frame += self.quantum_size as u64;
                self.frames_rendered.store(current_frame, Ordering::SeqCst);
                let ended_nodes = graph.take_ended_nodes(&context);
                if !ended_nodes.is_empty() {
                    self.post_to_control_thread(Notification::SourcesEnded(ended_nodes));
                }
            }
            self.post_to_control_thread(Notification::Complete(job));
        }
    }
}
