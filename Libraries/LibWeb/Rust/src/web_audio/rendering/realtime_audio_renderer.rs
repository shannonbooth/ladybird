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
    fn web_audio_output_open(factory: *mut c_void, renderer: *const c_void, generation: u64);
    fn web_audio_output_resume(stream: *mut c_void);
    fn web_audio_output_drain_and_suspend(stream: *mut c_void);
    fn web_audio_output_discard_and_suspend(stream: *mut c_void);
    fn web_audio_output_playback_underruns(stream: *mut c_void, duration: *mut f64, events: *mut u64);
    fn web_audio_monotonic_now_nanoseconds() -> i64;
}

pub trait RealtimeAudioRendererClient {
    fn sources_ended(&self, nodes: &[u64]);
    fn output_interrupted(&self);
    fn output_recovered(&self);
}

#[derive(Default)]
struct OutputTiming {
    stream_generation: u64,
    context_start_time: f64,
    context_end_time: f64,
    requested_at_nanoseconds: i64,
    plays_at_nanoseconds: i64,
    latency_reset_frame: u64,
    average_latency: f64,
    minimum_latency: f64,
    maximum_latency: f64,
}

#[derive(Default)]
struct OutputAnchor {
    version: AtomicU64,
    stream_generation: AtomicU64,
    context_start_time: AtomicU64,
    context_end_time: AtomicU64,
    requested_at_nanoseconds: AtomicI64,
    plays_at_nanoseconds: AtomicI64,
    latency_reset_frame: AtomicU64,
    average_latency: AtomicU64,
    minimum_latency: AtomicU64,
    maximum_latency: AtomicU64,
}

impl OutputAnchor {
    fn store(&self, sequence: u64, timing: OutputTiming) {
        let version = sequence.wrapping_mul(2);
        self.version.store(version.wrapping_sub(1), Ordering::Relaxed);
        fence(Ordering::SeqCst);
        self.stream_generation
            .store(timing.stream_generation, Ordering::Relaxed);
        self.context_start_time
            .store(timing.context_start_time.to_bits(), Ordering::Relaxed);
        self.context_end_time
            .store(timing.context_end_time.to_bits(), Ordering::Relaxed);
        self.requested_at_nanoseconds
            .store(timing.requested_at_nanoseconds, Ordering::Relaxed);
        self.plays_at_nanoseconds
            .store(timing.plays_at_nanoseconds, Ordering::Relaxed);
        self.latency_reset_frame
            .store(timing.latency_reset_frame, Ordering::Relaxed);
        self.average_latency
            .store(timing.average_latency.to_bits(), Ordering::Relaxed);
        self.minimum_latency
            .store(timing.minimum_latency.to_bits(), Ordering::Relaxed);
        self.maximum_latency
            .store(timing.maximum_latency.to_bits(), Ordering::Relaxed);
        self.version.store(version, Ordering::Release);
    }

    fn read(&self, sequence: u64) -> Option<OutputTiming> {
        let expected_version = sequence.wrapping_mul(2);
        if self.version.load(Ordering::Acquire) != expected_version {
            return None;
        }
        let copy = OutputTiming {
            stream_generation: self.stream_generation.load(Ordering::Relaxed),
            context_start_time: f64::from_bits(self.context_start_time.load(Ordering::Relaxed)),
            context_end_time: f64::from_bits(self.context_end_time.load(Ordering::Relaxed)),
            requested_at_nanoseconds: self.requested_at_nanoseconds.load(Ordering::Relaxed),
            plays_at_nanoseconds: self.plays_at_nanoseconds.load(Ordering::Relaxed),
            latency_reset_frame: self.latency_reset_frame.load(Ordering::Relaxed),
            average_latency: f64::from_bits(self.average_latency.load(Ordering::Relaxed)),
            minimum_latency: f64::from_bits(self.minimum_latency.load(Ordering::Relaxed)),
            maximum_latency: f64::from_bits(self.maximum_latency.load(Ordering::Relaxed)),
        };
        fence(Ordering::Acquire);
        (self.version.load(Ordering::Relaxed) == expected_version).then_some(copy)
    }
}

impl OutputTiming {
    fn latency(&self) -> f64 {
        (self.plays_at_nanoseconds - self.requested_at_nanoseconds).max(0) as f64 / 1e9
    }
}

// Keep enough anchors to look past the native stream's queued buffers without allocating in its callback.
const OUTPUT_HISTORY_CAPACITY: usize = 128;

struct OutputHistory {
    latest_sequence: AtomicU64,
    anchors: [OutputAnchor; OUTPUT_HISTORY_CAPACITY],
}

impl Default for OutputHistory {
    fn default() -> Self {
        Self {
            latest_sequence: AtomicU64::new(0),
            anchors: std::array::from_fn(|_| OutputAnchor::default()),
        }
    }
}

impl OutputHistory {
    fn store(&self, timing: OutputTiming) {
        let sequence = self.latest_sequence.load(Ordering::Relaxed) + 1;
        self.anchors[sequence as usize % OUTPUT_HISTORY_CAPACITY].store(sequence, timing);
        self.latest_sequence.store(sequence, Ordering::Release);
    }

    fn recent_anchors(&self) -> impl Iterator<Item = OutputTiming> + '_ {
        let latest = self.latest_sequence.load(Ordering::Acquire);
        let oldest = latest.saturating_sub(OUTPUT_HISTORY_CAPACITY as u64 - 1).max(1);
        (oldest..=latest)
            .rev()
            .filter_map(|sequence| self.anchors[sequence as usize % OUTPUT_HISTORY_CAPACITY].read(sequence))
    }

    fn latest(&self) -> Option<OutputTiming> {
        self.recent_anchors().next()
    }

    fn last_played(&self, stream_generation: u64, now_nanoseconds: i64) -> Option<OutputTiming> {
        self.recent_anchors()
            .take_while(|timing| timing.stream_generation == stream_generation)
            .find(|timing| timing.plays_at_nanoseconds <= now_nanoseconds)
    }
}

#[derive(Default)]
struct LatencyStatistics {
    reset_frame: u64,
    frames: f64,
    weighted_latency: f64,
    minimum_latency: f64,
    maximum_latency: f64,
}

struct RenderState {
    graph: RenderGraph,
    device_bus: Option<Box<AudioBus>>,
    device_channel_count: usize,
    playhead_step: f64,
    playhead: f64,
    pending_samples: Vec<f32>,
    latency_statistics: LatencyStatistics,
}

struct DeviceConfiguration {
    stream: SharedCppObject,
    channel_count: usize,
    sample_rate: u32,
    generation: u64,
}

// Renders an AudioContext's graph in real time, pulled by the audio output device: the playback stream's data request
// callback runs on the platform's audio thread and renders just enough render quanta to fill the device's buffer.
// Progress is reported back to the control thread by posting events to the main thread's event loop.
// https://webaudio.github.io/web-audio-api/#AudioContext
pub struct RealtimeAudioRenderer {
    control_message_queue: Arc<ControlMessageQueue>,
    sample_rate: f32,
    quantum_size: usize,
    audio_output_factory: SharedCppObject,
    playback_stream: Mutex<Option<SharedCppObject>>,
    render_state_access: AtomicU64,
    device_configuration_waiting: AtomicBool,
    pending_device_configuration: ControlThreadCell<Option<DeviceConfiguration>>,
    control_thread: ControlThread,
    client: ControlThreadCell<Option<Rc<dyn RealtimeAudioRendererClient>>>,
    frames_rendered: AtomicU64,
    suspended: AtomicBool,
    output_interrupted: AtomicBool,
    shutting_down: AtomicBool,
    output_history: OutputHistory,
    latency_reset_frame: AtomicU64,
    retired_underruns: ControlThreadCell<(f64, u64)>,
    retired_playback_latency: ControlThreadCell<f64>,
    state: UnsafeCell<RenderState>,
}

// SAFETY: The render-state gate gives an output callback or a stream replacement exclusive access to the state.
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
        let mut graph = RenderGraph::default();
        graph.set_destination(destination_node_id);
        Arc::new(Self {
            control_message_queue: queue,
            sample_rate,
            quantum_size,
            audio_output_factory,
            playback_stream: Mutex::new(None),
            render_state_access: AtomicU64::new(0),
            device_configuration_waiting: AtomicBool::new(false),
            pending_device_configuration: ControlThreadCell::new(None),
            control_thread: ControlThread::current(),
            client: ControlThreadCell::new(None),
            frames_rendered: AtomicU64::new(0),
            suspended: AtomicBool::new(false),
            output_interrupted: AtomicBool::new(false),
            shutting_down: AtomicBool::new(false),
            output_history: OutputHistory::default(),
            latency_reset_frame: AtomicU64::new(0),
            retired_underruns: ControlThreadCell::new((0., 0)),
            retired_playback_latency: ControlThreadCell::new(0.),
            state: UnsafeCell::new(RenderState {
                graph,
                device_bus: None,
                device_channel_count: 0,
                playhead_step: 1.,
                playhead: 0.,
                pending_samples: Vec::new(),
                latency_statistics: LatencyStatistics::default(),
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
        let generation = self.render_state_access.fetch_add(2, Ordering::SeqCst).wrapping_add(2) >> 1;
        unsafe { web_audio_output_open(self.audio_output_factory.pointer, Arc::as_ptr(self).cast(), generation) };
    }

    fn set_playback_stream(self: &Arc<Self>, configuration: DeviceConfiguration) {
        if self.shutting_down.load(Ordering::SeqCst) || configuration.generation != self.stream_generation() {
            unsafe { web_audio_output_discard_and_suspend(configuration.stream.pointer) };
            return;
        }

        if !self.acquire_render_state(configuration.generation) {
            // The previous stream may still have a callback in flight.
            self.pending_device_configuration.replace(Some(configuration));
            self.device_configuration_waiting.store(true, Ordering::SeqCst);
            // If the callback finished before seeing the pending configuration, apply it here.
            if self.render_state_access.load(Ordering::SeqCst) & 1 == 0
                && self.device_configuration_waiting.swap(false, Ordering::SeqCst)
            {
                self.apply_pending_device_configuration();
            }
            return;
        }

        let DeviceConfiguration {
            stream,
            channel_count,
            sample_rate,
            ..
        } = configuration;
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
        self.release_render_state();
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

    fn stream_generation(&self) -> u64 {
        self.render_state_access.load(Ordering::SeqCst) >> 1
    }

    fn acquire_render_state(&self, generation: u64) -> bool {
        // The low bit is the gate, so an old callback cannot acquire a newer generation's state.
        let idle = generation << 1;
        self.render_state_access
            .compare_exchange(idle, idle | 1, Ordering::SeqCst, Ordering::SeqCst)
            .is_ok()
    }

    fn release_render_state(self: &Arc<Self>) {
        self.render_state_access.fetch_and(!1, Ordering::SeqCst);
        if self.device_configuration_waiting.swap(false, Ordering::SeqCst) {
            let renderer = Arc::clone(self);
            self.control_thread
                .post(move || renderer.apply_pending_device_configuration());
        }
    }

    fn apply_pending_device_configuration(self: &Arc<Self>) {
        if let Some(configuration) = self.pending_device_configuration.replace(None) {
            self.set_playback_stream(configuration);
        }
    }

    pub fn output_is_interrupted(&self) -> bool {
        self.output_interrupted.load(Ordering::Acquire)
    }

    fn playback_stream(&self) -> Option<SharedCppObject> {
        self.playback_stream.lock().unwrap().clone()
    }

    // The device went away with the process behind it: a new stream picks the graph up where it stands.
    fn output_lost(self: &Arc<Self>, generation: u64) {
        if self.shutting_down.load(Ordering::SeqCst) || generation != self.stream_generation() {
            return;
        }
        let underruns = self.playback_underruns();
        self.retired_underruns.replace(underruns);
        self.retired_playback_latency.replace(self.last_played_latency());
        *self.playback_stream.lock().unwrap() = None;
        self.pending_device_configuration.replace(None);
        if !self.output_interrupted.swap(true, Ordering::AcqRel)
            && let Some(client) = self.client.get()
        {
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
        self.retired_playback_latency.replace(self.last_played_latency());
        self.shutting_down.store(true, Ordering::SeqCst);
        self.pending_device_configuration.replace(None);
        let stream = self.playback_stream.lock().unwrap().take();
        if let Some(stream) = stream {
            unsafe { web_audio_output_discard_and_suspend(stream.pointer) };
        }
    }

    pub fn frames_rendered(&self) -> u64 {
        self.frames_rendered.load(Ordering::SeqCst)
    }

    pub fn output_timestamp(&self) -> Option<(f64, i64)> {
        let timing = self.output_history.latest()?;
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
        self.output_history.latest().map_or(0., |timing| timing.latency())
    }

    pub fn last_played_latency(&self) -> f64 {
        self.playback_latency_at(unsafe { web_audio_monotonic_now_nanoseconds() })
    }

    fn playback_latency_at(&self, now_nanoseconds: i64) -> f64 {
        if !self.shutting_down.load(Ordering::Acquire)
            && let Some(timing) = self
                .output_history
                .last_played(self.stream_generation(), now_nanoseconds)
        {
            return timing.latency();
        }
        self.retired_playback_latency.get()
    }

    pub fn playback_underruns(&self) -> (f64, u64) {
        let (mut duration, mut events) = (0., 0);
        if let Some(stream) = self.playback_stream() {
            unsafe { web_audio_output_playback_underruns(stream.pointer, &raw mut duration, &raw mut events) };
        }
        let retired = self.retired_underruns.get();
        (duration + retired.0, events.saturating_add(retired.1))
    }

    pub fn reset_playback_latency_statistics(&self, reset_time: f64) {
        let reset_frame = (reset_time * f64::from(self.sample_rate)).round() as u64;
        self.latency_reset_frame.store(reset_frame, Ordering::Release);
    }

    pub fn playback_latency_statistics(&self) -> Option<(f64, f64, f64)> {
        let timing = self.output_history.latest()?;
        if timing.latency_reset_frame != self.latency_reset_frame.load(Ordering::Acquire) {
            return None;
        }
        Some((timing.average_latency, timing.minimum_latency, timing.maximum_latency))
    }

    fn fill_output_buffer(
        self: &Arc<Self>,
        generation: u64,
        buffer: &mut [f32],
        buffer_starts_playing_at: i64,
    ) -> usize {
        if !self.acquire_render_state(generation) {
            return 0;
        }
        let written = unsafe { self.render_output_buffer(generation, buffer, buffer_starts_playing_at) };
        self.release_render_state();
        written
    }

    unsafe fn render_output_buffer(
        self: &Arc<Self>,
        stream_generation: u64,
        buffer: &mut [f32],
        buffer_starts_playing_at: i64,
    ) -> usize {
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

        let reset_frame = self.latency_reset_frame.load(Ordering::Acquire);
        let latency = (buffer_starts_playing_at - requested_at_nanoseconds).max(0) as f64 / 1e9;
        let statistics = &mut state.latency_statistics;
        if reset_frame != statistics.reset_frame {
            *statistics = LatencyStatistics {
                reset_frame,
                ..Default::default()
            };
        }
        let frames = (context_end_frame - context_start_frame.max(reset_frame as f64)).max(0.);
        if frames > 0. {
            if statistics.frames == 0. {
                statistics.minimum_latency = latency;
                statistics.maximum_latency = latency;
            } else {
                statistics.minimum_latency = statistics.minimum_latency.min(latency);
                statistics.maximum_latency = statistics.maximum_latency.max(latency);
            }
            statistics.frames += frames;
            statistics.weighted_latency += frames * latency;
        }
        self.output_history.store(OutputTiming {
            stream_generation,
            context_start_time: context_start_frame / sample_rate,
            context_end_time: context_end_frame / sample_rate,
            requested_at_nanoseconds,
            plays_at_nanoseconds: buffer_starts_playing_at,
            latency_reset_frame: if statistics.frames > 0. { reset_frame } else { u64::MAX },
            average_latency: if statistics.frames > 0. {
                (statistics.weighted_latency / statistics.frames)
                    .clamp(statistics.minimum_latency, statistics.maximum_latency)
            } else {
                0.
            },
            minimum_latency: statistics.minimum_latency,
            maximum_latency: statistics.maximum_latency,
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
    generation: u64,
    output: ak::SpanMut<'_, f32>,
    plays_at_nanoseconds: i64,
) -> usize {
    let renderer = unsafe { clone_arc(renderer) };
    renderer.fill_output_buffer(generation, output.into_slice(), plays_at_nanoseconds)
}

#[unsafe(no_mangle)]
pub(crate) unsafe extern "C" fn web_audio_realtime_renderer_output_opened(
    renderer: *const RealtimeAudioRenderer,
    generation: u64,
    stream: FfiSharedCppObject,
    channel_count: usize,
    sample_rate: u32,
) {
    let renderer = unsafe { clone_arc(renderer) };
    let stream = unsafe { stream.acquire() }.expect("an opened output has a stream");
    renderer.set_playback_stream(DeviceConfiguration {
        stream,
        channel_count,
        sample_rate,
        generation,
    });
}

#[unsafe(no_mangle)]
pub(crate) unsafe extern "C" fn web_audio_realtime_renderer_output_lost(
    renderer: *const RealtimeAudioRenderer,
    generation: u64,
) {
    unsafe { clone_arc(renderer) }.output_lost(generation);
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::css::style::pipeline::allocation_count::count_allocations;
    use crate::web_audio::rendering::audio_worklet_pipe_tests::run_posted_tasks;

    #[unsafe(no_mangle)]
    extern "C" fn web_audio_monotonic_now_nanoseconds() -> i64 {
        0
    }

    #[unsafe(no_mangle)]
    extern "C" fn web_audio_output_open(_: *mut c_void, _: *const c_void, _: u64) {}

    #[unsafe(no_mangle)]
    extern "C" fn web_audio_output_resume(_: *mut c_void) {}

    #[unsafe(no_mangle)]
    extern "C" fn web_audio_output_discard_and_suspend(_: *mut c_void) {}

    #[unsafe(no_mangle)]
    extern "C" fn web_audio_output_playback_underruns(_: *mut c_void, _: *mut f64, _: *mut u64) {}

    #[test]
    fn playback_latency_uses_the_last_presented_buffer() {
        let history = OutputHistory::default();
        history.store(OutputTiming {
            requested_at_nanoseconds: 10_000_000,
            plays_at_nanoseconds: 20_000_000,
            ..Default::default()
        });
        history.store(OutputTiming {
            requested_at_nanoseconds: 25_000_000,
            plays_at_nanoseconds: 40_000_000,
            ..Default::default()
        });
        assert!(history.last_played(0, 19_999_999).is_none());
        assert_eq!(history.last_played(0, 30_000_000).unwrap().latency(), 0.010);
        assert_eq!(history.last_played(0, 40_000_000).unwrap().latency(), 0.015);
    }

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
        renderer.set_playback_stream(DeviceConfiguration {
            stream: handle.clone(),
            channel_count: 2,
            sample_rate: 48000,
            generation: 0,
        });

        let state = unsafe { &mut *renderer.state.get() };
        state.pending_samples.extend([1., -1., 2., -2.]);
        state.playhead = 1.5;

        let old_generation = renderer.stream_generation();
        renderer.output_history.store(OutputTiming {
            stream_generation: old_generation,
            requested_at_nanoseconds: -20_000_000,
            plays_at_nanoseconds: -10_000_000,
            ..Default::default()
        });
        renderer.output_history.store(OutputTiming {
            stream_generation: old_generation,
            requested_at_nanoseconds: -5_000_000,
            plays_at_nanoseconds: 10_000_000,
            ..Default::default()
        });
        assert!(renderer.acquire_render_state(old_generation));
        renderer.output_lost(old_generation);
        assert!(renderer.output_is_interrupted());
        renderer.set_playback_stream(DeviceConfiguration {
            stream: handle,
            channel_count: 1,
            sample_rate: 44100,
            generation: 1,
        });
        assert!(renderer.output_is_interrupted());
        assert_eq!(unsafe { &*renderer.state.get() }.device_channel_count, 2);

        std::thread::scope(|scope| {
            scope.spawn(|| renderer.release_render_state());
        });
        run_posted_tasks();
        assert!(!renderer.output_is_interrupted());
        // The old callback could have observed its generation before the stream was replaced.
        assert!(!renderer.acquire_render_state(old_generation));
        assert_eq!(renderer.fill_output_buffer(old_generation, &mut [0.; 4], 0), 0);
        renderer.output_lost(old_generation);
        assert!(!renderer.output_is_interrupted());

        let state = unsafe { &*renderer.state.get() };
        assert_eq!(state.device_channel_count, 1);
        assert!(state.pending_samples.is_empty());
        assert_eq!(state.playhead, 0.);
        assert_eq!(&*client.0.borrow(), &["interrupted", "recovered"]);
        assert_eq!(renderer.playback_latency_at(20_000_000), 0.010);
        assert_eq!(renderer.fill_output_buffer(1, &mut [0.; 4], 30_000_000), 4);
        assert_eq!(renderer.playback_latency_at(20_000_000), 0.010);
        assert_eq!(renderer.playback_latency_at(30_000_000), 0.030);
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
            let (samples_written, allocations) = count_allocations(|| renderer.fill_output_buffer(0, &mut buffer, 0));
            assert_eq!(samples_written, buffer.len());
            assert_eq!(allocations.everywhere, 0);
            assert!(buffer.iter().all(|&sample| sample == 0.));
        }
    }
}
