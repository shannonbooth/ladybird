/*
 * Copyright (c) 2026, Jelle Raaijmakers <jelle@ladybird.org>
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#![allow(clippy::needless_range_loop, clippy::manual_memcpy)]
use std::cell::UnsafeCell;
use std::ffi::c_void;
use std::rc::Rc;
use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU64, Ordering, fence};
use std::sync::{Arc, Mutex};

use super::audio_bus::AudioBus;
use super::node_message::SharedCppObject;
use super::render_graph::RenderGraph;
use super::render_node::RenderContext;
use crate::web_audio::control_message::{FfiSharedCppObject, clone_arc};
use crate::web_audio::control_message_queue::ControlMessageQueue;
use crate::web_audio::control_thread::{ControlThread, ControlThreadCell};

unsafe extern "C" {
    fn web_audio_output_open(factory: *mut c_void, renderer: *const c_void);
    fn web_audio_output_resume(stream: *mut c_void);
    fn web_audio_output_drain_and_suspend(stream: *mut c_void);
    fn web_audio_output_discard_and_suspend(stream: *mut c_void);
    fn web_audio_monotonic_now_nanoseconds() -> i64;
}

pub trait RealtimeAudioRendererClient {
    fn sources_ended(&self, nodes: &[u64]);
    fn output_interrupted(&self);
    fn output_recovered(&self);
}

struct OutputTiming {
    context_start_time: f64,
    context_end_time: f64,
    requested_at_nanoseconds: i64,
    plays_at_nanoseconds: i64,
}

// The scheduled presentation time and context frame range of the latest device buffer.
#[derive(Default)]
struct OutputAnchor {
    version: AtomicU64,
    context_start_time: AtomicU64,
    context_end_time: AtomicU64,
    requested_at_nanoseconds: AtomicI64,
    plays_at_nanoseconds: AtomicI64,
}

impl OutputAnchor {
    fn store(&self, timing: OutputTiming) {
        let version = self.version.load(Ordering::Relaxed);
        self.version.store(version.wrapping_add(1), Ordering::Relaxed);
        fence(Ordering::SeqCst);
        self.context_start_time
            .store(timing.context_start_time.to_bits(), Ordering::Relaxed);
        self.context_end_time
            .store(timing.context_end_time.to_bits(), Ordering::Relaxed);
        self.requested_at_nanoseconds
            .store(timing.requested_at_nanoseconds, Ordering::Relaxed);
        self.plays_at_nanoseconds
            .store(timing.plays_at_nanoseconds, Ordering::Relaxed);
        self.version.store(version.wrapping_add(2), Ordering::Release);
    }

    fn read(&self) -> Option<OutputTiming> {
        for _retry in 0..32 {
            let version_before = self.version.load(Ordering::Acquire);
            if version_before == 0 {
                return None;
            }
            if !version_before.is_multiple_of(2) {
                continue;
            }
            let copy = OutputTiming {
                context_start_time: f64::from_bits(self.context_start_time.load(Ordering::Relaxed)),
                context_end_time: f64::from_bits(self.context_end_time.load(Ordering::Relaxed)),
                requested_at_nanoseconds: self.requested_at_nanoseconds.load(Ordering::Relaxed),
                plays_at_nanoseconds: self.plays_at_nanoseconds.load(Ordering::Relaxed),
            };
            fence(Ordering::Acquire);
            let version_after = self.version.load(Ordering::Relaxed);
            if version_before == version_after {
                return Some(copy);
            }
        }
        None
    }
}

// State below is only used on the audio thread, except that the device configuration is recorded on the control
// thread after stream creation, before the first data request callback can run.
struct RenderState {
    graph: RenderGraph,
    device_bus: Option<Box<AudioBus>>,
    device_channel_count: usize,
    playhead_step: f64,
    playhead: f64,
    pending_samples: Vec<f32>,
}

// Renders an AudioContext's graph in real time, pulled by the audio output device: the playback stream's data request
// callback runs on the platform's audio thread and renders just enough render quanta to fill the device's buffer.
// Progress is reported back to the control thread by posting events to the main thread's event loop.
// https://webaudio.github.io/web-audio-api/#AudioContext
pub struct RealtimeAudioRenderer {
    control_message_queue: Arc<ControlMessageQueue>,
    destination_node_id: u64,
    sample_rate: f32,
    quantum_size: usize,
    audio_output_factory: SharedCppObject,
    playback_stream: Mutex<Option<SharedCppObject>>,
    control_thread: ControlThread,
    client: ControlThreadCell<Option<Rc<dyn RealtimeAudioRendererClient>>>,
    frames_rendered: AtomicU64,
    suspended: AtomicBool,
    output_interrupted: AtomicBool,
    shutting_down: AtomicBool,
    output_anchor: OutputAnchor,
    state: UnsafeCell<RenderState>,
}

// SAFETY: Only the audio thread mutates render state after the suspended stream's device configuration is recorded.
unsafe impl Send for RealtimeAudioRenderer {}
unsafe impl Sync for RealtimeAudioRenderer {}
impl RealtimeAudioRenderer {
    pub fn new(
        queue: Arc<ControlMessageQueue>,
        destination_node_id: u64,
        sample_rate: f32,
        quantum_size: usize,
        audio_output_factory: SharedCppObject,
    ) -> Arc<Self> {
        Arc::new(Self {
            control_message_queue: queue,
            destination_node_id,
            sample_rate,
            quantum_size,
            audio_output_factory,
            playback_stream: Mutex::new(None),
            control_thread: ControlThread::current(),
            client: ControlThreadCell::new(None),
            frames_rendered: AtomicU64::new(0),
            suspended: AtomicBool::new(false),
            output_interrupted: AtomicBool::new(false),
            shutting_down: AtomicBool::new(false),
            output_anchor: OutputAnchor::default(),
            state: UnsafeCell::new(RenderState {
                graph: RenderGraph::default(),
                device_bus: None,
                device_channel_count: 0,
                playhead_step: 1.,
                playhead: 0.,
                pending_samples: Vec::new(),
            }),
        })
    }

    /// The callback is set on the control thread before rendering starts and is always invoked on the control thread. It
    /// is cleared when the context is suspended, closes, or its document becomes inactive.
    pub fn set_client(&self, client: Option<Rc<dyn RealtimeAudioRendererClient>>) {
        self.client.replace(client);
    }

    pub fn start_rendering(self: &Arc<Self>) {
        assert!(self.playback_stream.lock().unwrap().is_none());
        unsafe { &mut *self.state.get() }
            .graph
            .set_destination(self.destination_node_id);

        // The stream is created in a suspended state so the device configuration can be recorded before the first data
        // request callback runs on the audio thread.
        unsafe { web_audio_output_open(self.audio_output_factory.pointer, Arc::as_ptr(self).cast()) };
    }

    fn set_playback_stream(self: &Arc<Self>, stream: SharedCppObject, channel_count: usize, sample_rate: u32) {
        if self.shutting_down.load(Ordering::SeqCst) {
            unsafe { web_audio_output_discard_and_suspend(stream.pointer) };
            return;
        }
        let state = unsafe { &mut *self.state.get() };
        if state.device_channel_count != channel_count {
            state.pending_samples.clear();
            state.playhead = 0.;
        }
        state.device_channel_count = channel_count;
        let pending_capacity = (self.quantum_size + 1) * channel_count;
        state
            .pending_samples
            .reserve_exact(pending_capacity.saturating_sub(state.pending_samples.len()));
        state.device_bus = Some(Box::new(AudioBus::new(channel_count, self.quantum_size)));

        // When the device sample rate differs from the context's, the rendered audio is resampled by advancing
        // through the rendered frames at this rate.
        state.playhead_step = f64::from(self.sample_rate) / f64::from(sample_rate);
        *self.playback_stream.lock().unwrap() = Some(stream.clone());
        if !self.suspended.load(Ordering::SeqCst) {
            unsafe { web_audio_output_resume(stream.pointer) };
        }
        if self.output_interrupted.swap(false, Ordering::AcqRel)
            && let Some(client) = self.client.get()
        {
            client.output_recovered();
        }
    }

    pub fn output_is_interrupted(&self) -> bool {
        self.output_interrupted.load(Ordering::Acquire)
    }

    fn playback_stream(&self) -> Option<SharedCppObject> {
        self.playback_stream.lock().unwrap().clone()
    }

    // The device went away with the process behind it: a new stream picks the graph up where it stands.
    fn output_lost(self: &Arc<Self>) {
        if self.shutting_down.load(Ordering::SeqCst) {
            return;
        }
        *self.playback_stream.lock().unwrap() = None;
        if self.output_interrupted.swap(true, Ordering::AcqRel) {
            return;
        }
        if let Some(client) = self.client.get() {
            client.output_interrupted();
        }
        self.start_rendering();
    }

    pub fn resume(&self) {
        self.suspended.store(false, Ordering::SeqCst);
        if let Some(stream) = self.playback_stream() {
            unsafe { web_audio_output_resume(stream.pointer) };
        }
    }

    pub fn suspend(&self) {
        self.suspended.store(true, Ordering::SeqCst);
        if let Some(stream) = self.playback_stream() {
            unsafe { web_audio_output_drain_and_suspend(stream.pointer) };
        }
    }

    pub fn stop(&self) {
        self.shutting_down.store(true, Ordering::SeqCst);
        let stream = self.playback_stream.lock().unwrap().take();
        if let Some(stream) = stream {
            unsafe { web_audio_output_discard_and_suspend(stream.pointer) };
        }
    }

    pub fn frames_rendered(&self) -> u64 {
        self.frames_rendered.load(Ordering::SeqCst)
    }

    pub fn output_timestamp(&self) -> Option<(f64, i64)> {
        let timing = self.output_anchor.read()?;
        let now_nanoseconds = unsafe { web_audio_monotonic_now_nanoseconds() };
        let elapsed = ((now_nanoseconds - timing.plays_at_nanoseconds) as f64 / 1e9).clamp(
            -timing.context_start_time,
            timing.context_end_time - timing.context_start_time,
        );
        Some((
            timing.context_start_time + elapsed,
            timing.plays_at_nanoseconds + (elapsed * 1e9) as i64,
        ))
    }

    pub fn output_latency(&self) -> f64 {
        self.output_anchor.read().map_or(0., |timing| {
            (timing.plays_at_nanoseconds - timing.requested_at_nanoseconds).max(0) as f64 / 1e9
        })
    }

    unsafe fn fill_output_buffer(self: &Arc<Self>, buffer: &mut [f32], buffer_starts_playing_at: i64) -> usize {
        let state = unsafe { &mut *self.state.get() };
        if self.shutting_down.load(Ordering::SeqCst) || state.device_channel_count == 0 {
            return 0;
        }
        let channel_count = state.device_channel_count;
        let frames_requested = buffer.len() / channel_count;
        let requested_at_nanoseconds = unsafe { web_audio_monotonic_now_nanoseconds() };
        let pending_frames = state.pending_samples.len() / channel_count;
        let context_start_frame =
            self.frames_rendered.load(Ordering::Relaxed) as f64 - pending_frames as f64 + state.playhead;
        let context_end_frame = context_start_frame + frames_requested as f64 * state.playhead_step;
        let sample_rate = f64::from(self.sample_rate);
        for frame in 0..frames_requested {
            let mut first_frame = state.playhead as usize;
            let fraction = (state.playhead - first_frame as f64) as f32;

            // Keep at most one sample frame from the previous quantum for interpolation.
            while state.pending_samples.len() / channel_count < first_frame + 2 {
                let consumed_frames = first_frame.min(state.pending_samples.len() / channel_count);
                state.pending_samples.drain(..consumed_frames * channel_count);
                state.playhead -= consumed_frames as f64;
                first_frame -= consumed_frames;
                self.render_quantum_into_pending_samples(state);
            }
            for channel in 0..channel_count {
                let mut sample = state.pending_samples[first_frame * channel_count + channel];
                if fraction != 0. {
                    let next_sample = state.pending_samples[(first_frame + 1) * channel_count + channel];
                    sample += fraction * (next_sample - sample);
                }
                buffer[frame * channel_count + channel] = sample;
            }
            state.playhead += state.playhead_step;
        }

        self.output_anchor.store(OutputTiming {
            context_start_time: context_start_frame / sample_rate,
            context_end_time: context_end_frame / sample_rate,
            requested_at_nanoseconds,
            plays_at_nanoseconds: buffer_starts_playing_at,
        });
        buffer.len()
    }

    fn render_quantum_into_pending_samples(self: &Arc<Self>, state: &mut RenderState) {
        state.graph.apply_control_messages(self.control_message_queue.drain());
        let context = RenderContext {
            sample_rate: f64::from(self.sample_rate),
            quantum_size: self.quantum_size,
            quantum_start_frame: self.frames_rendered.load(Ordering::SeqCst),
        };
        state.graph.render_quantum(&context);

        // Mix the destination's output to the stream's channel layout and append it as interleaved samples.
        if let Some(device_bus) = &mut state.device_bus {
            device_bus.zero();
            if let Some(destination_node) = state.graph.destination() {
                device_bus.sum_from(unsafe { &*destination_node.data().output(0) }, true);
            }
            let sample_offset = state.pending_samples.len();
            debug_assert!(
                sample_offset + self.quantum_size * state.device_channel_count <= state.pending_samples.capacity()
            );
            state
                .pending_samples
                .resize(sample_offset + self.quantum_size * state.device_channel_count, 0.);
            for frame in 0..self.quantum_size {
                for channel in 0..state.device_channel_count {
                    state.pending_samples[sample_offset + frame * state.device_channel_count + channel] =
                        device_bus.channel(channel)[frame];
                }
            }
        }
        self.frames_rendered
            .fetch_add(self.quantum_size as u64, Ordering::SeqCst);
        let ended_nodes = state.graph.take_ended_nodes(&context);
        if !ended_nodes.is_empty() {
            let renderer = Arc::clone(self);
            self.control_thread.post(move || {
                if let Some(client) = renderer.client.get() {
                    client.sources_ended(&ended_nodes);
                }
            });
        }
    }
}

#[unsafe(no_mangle)]
pub(crate) unsafe extern "C" fn web_audio_realtime_renderer_retain(renderer: *const RealtimeAudioRenderer) {
    unsafe { Arc::increment_strong_count(renderer) };
}

#[unsafe(no_mangle)]
pub(crate) unsafe extern "C" fn web_audio_realtime_renderer_release(renderer: *const RealtimeAudioRenderer) {
    unsafe { Arc::decrement_strong_count(renderer) };
}

#[unsafe(no_mangle)]
pub(crate) unsafe extern "C" fn web_audio_realtime_renderer_fill(
    renderer: *const RealtimeAudioRenderer,
    output: ak::SpanMut<'_, f32>,
    plays_at_nanoseconds: i64,
) -> usize {
    let renderer = unsafe { clone_arc(renderer) };
    unsafe { renderer.fill_output_buffer(output.into_slice(), plays_at_nanoseconds) }
}

#[unsafe(no_mangle)]
pub(crate) unsafe extern "C" fn web_audio_realtime_renderer_output_opened(
    renderer: *const RealtimeAudioRenderer,
    stream: FfiSharedCppObject,
    channel_count: usize,
    sample_rate: u32,
) {
    let renderer = unsafe { clone_arc(renderer) };
    let stream = unsafe { stream.acquire() }.expect("an opened output has a stream");
    renderer.set_playback_stream(stream, channel_count, sample_rate);
}

#[unsafe(no_mangle)]
pub(crate) unsafe extern "C" fn web_audio_realtime_renderer_output_lost(renderer: *const RealtimeAudioRenderer) {
    unsafe { clone_arc(renderer) }.output_lost();
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::css::style::pipeline::allocation_count::count_allocations;

    #[unsafe(no_mangle)]
    extern "C" fn web_audio_monotonic_now_nanoseconds() -> i64 {
        0
    }

    #[unsafe(no_mangle)]
    extern "C" fn web_audio_output_open(_: *mut c_void, _: *const c_void) {}

    #[unsafe(no_mangle)]
    extern "C" fn web_audio_output_resume(_: *mut c_void) {}

    #[unsafe(no_mangle)]
    extern "C" fn web_audio_output_discard_and_suspend(_: *mut c_void) {}

    #[test]
    fn replacing_an_output_stream_reports_interruption_and_resets_channel_alignment() {
        struct Client(std::cell::RefCell<Vec<&'static str>>);
        impl RealtimeAudioRendererClient for Client {
            fn sources_ended(&self, _: &[u64]) {}
            fn output_interrupted(&self) {
                self.0.borrow_mut().push("interrupted");
            }
            fn output_recovered(&self) {
                self.0.borrow_mut().push("recovered");
            }
        }

        unsafe extern "C" fn no_op(_: *mut c_void) {}
        let handle = SharedCppObject {
            pointer: std::ptr::null_mut(),
            retain: no_op,
            release: no_op,
        };
        let renderer =
            RealtimeAudioRenderer::new(Arc::new(ControlMessageQueue::default()), 0, 48000., 4, handle.clone());
        let client = Rc::new(Client(std::cell::RefCell::new(Vec::new())));
        renderer.set_client(Some(client.clone()));
        renderer.set_playback_stream(handle.clone(), 2, 48000);

        let state = unsafe { &mut *renderer.state.get() };
        state.pending_samples.extend([1., -1., 2., -2.]);
        state.playhead = 1.5;

        renderer.output_lost();
        assert!(renderer.output_is_interrupted());
        renderer.set_playback_stream(handle, 1, 44100);
        assert!(!renderer.output_is_interrupted());

        let state = unsafe { &*renderer.state.get() };
        assert_eq!(state.device_channel_count, 1);
        assert!(state.pending_samples.is_empty());
        assert_eq!(state.playhead, 0.);
        assert_eq!(&*client.0.borrow(), &["interrupted", "recovered"]);
    }

    #[test]
    fn resamples_a_device_buffer_without_allocating() {
        unsafe extern "C" fn no_op(_: *mut c_void) {}

        let renderer = RealtimeAudioRenderer::new(
            Arc::new(ControlMessageQueue::default()),
            0,
            48000.,
            8,
            SharedCppObject {
                pointer: std::ptr::null_mut(),
                retain: no_op,
                release: no_op,
            },
        );
        let state = unsafe { &mut *renderer.state.get() };
        state.device_channel_count = 2;
        state.device_bus = Some(Box::new(AudioBus::new(2, renderer.quantum_size)));
        state.pending_samples.reserve_exact((renderer.quantum_size + 1) * 2);
        let mut buffer = [f32::NAN; 256];
        for playhead_step in [0.375, 27.5] {
            unsafe { &mut *renderer.state.get() }.playhead_step = playhead_step;
            let (samples_written, allocations) =
                count_allocations(|| unsafe { renderer.fill_output_buffer(&mut buffer, 0) });
            assert_eq!(samples_written, buffer.len());
            assert_eq!(allocations.everywhere, 0);
            assert!(buffer.iter().all(|&sample| sample == 0.));
        }
    }
}
