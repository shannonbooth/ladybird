/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::cell::{Cell, RefCell};
use std::ffi::c_void;
use std::time::{Duration, Instant};

use libjs_rust::api::{Trace, Vm};
use libweb_rust::web_audio::control_message::FfiSharedCppObject;
use libweb_rust::web_audio::rendering::node_message::SharedCppObject;

use super::audio_context::AudioContext;
use super::base_audio_context::AudioContextState;
use crate::bindings::interface::{Inherits, Ref, Wrappable, allocate, cell_from_abi, cell_into_abi};
use crate::bindings::promise::queue_a_microtask;
use crate::bindings::{CallContext, WebIdlResult};

// https://webaudio.github.io/web-audio-api/#AudioPlaybackStats
#[derive(Trace)]
#[gc(crate = libjs_rust)]
pub(crate) struct AudioPlaybackStats {
    audio_context: Ref<AudioContext>,
    underrun_duration: Cell<f64>,
    underrun_events: Cell<u32>,
    total_duration: Cell<f64>,
    average_latency: Cell<f64>,
    minimum_latency: Cell<f64>,
    maximum_latency: Cell<f64>,
    latency_reset_time: Cell<f64>,
    #[gc(untraced)]
    last_update: Cell<Option<Instant>>,
    held_until_microtask_checkpoint: Cell<bool>,
    this: Cell<Option<Ref<AudioPlaybackStats>>>,
    #[gc(untraced)]
    update_timer: RefCell<Option<SharedCppObject>>,
}

impl Wrappable for AudioPlaybackStats {}

unsafe extern "C" {
    fn web_audio_playback_stats_create_timer(stats: *mut c_void) -> FfiSharedCppObject;
    fn web_audio_playback_stats_can_update(global: *mut c_void) -> bool;
}

impl AudioPlaybackStats {
    pub(crate) fn create(vm: &Vm, audio_context: Ref<AudioContext>) -> Ref<Self> {
        let stats = allocate(vm, |_| Self {
            audio_context,
            underrun_duration: Cell::new(0.),
            underrun_events: Cell::new(0),
            total_duration: Cell::new(0.),
            average_latency: Cell::new(0.),
            minimum_latency: Cell::new(0.),
            maximum_latency: Cell::new(0.),
            latency_reset_time: Cell::new(0.),
            last_update: Cell::new(None),
            held_until_microtask_checkpoint: Cell::new(false),
            this: Cell::new(None),
            update_timer: RefCell::new(None),
        });
        stats.this.set(Some(stats));
        let timer = unsafe { web_audio_playback_stats_create_timer(cell_into_abi(stats)) };
        *stats.update_timer.borrow_mut() = Some(SharedCppObject {
            pointer: timer.pointer,
            retain: timer.retain,
            release: timer.release,
        });
        stats
    }

    pub(crate) fn stop_updates(&self) {
        self.update_timer.take();
    }

    // AD-HOC: Bring stale stats up to date when script reads them, but keep them unchanged until the next microtask
    //         checkpoint, as required by WPT.
    fn refresh(&self, vm: &Vm) {
        if self.held_until_microtask_checkpoint.replace(true) {
            return;
        }
        let this = self
            .this
            .get()
            .expect("playback stats know themselves after allocation");
        queue_a_microtask(vm, self.audio_context.parent().realm(), this, |_, stats| {
            stats.held_until_microtask_checkpoint.set(false);
        });
        if self
            .last_update
            .get()
            .is_none_or(|last_update| last_update.elapsed() >= Duration::from_secs(1))
        {
            self.update_audio_stats();
        }
    }

    // https://webaudio.github.io/web-audio-api/#update-audio-stats
    fn update_audio_stats(&self) {
        self.last_update.set(Some(Instant::now()));

        // 1. If [[audio context]] is not running, abort these steps.
        if self.audio_context.parent().state_value() != AudioContextState::Running {
            return;
        }

        // 2. Let canUpdate be false.
        // 3. Let document be the current this’s relevant global object’s associated Document. If document is fully active and document’s visibility state is "visible", set canUpdate to true.
        // 4. Let permission be the permission state for the permission associated with "microphone" access. If permission is "granted", set canUpdate to true.
        let can_update =
            unsafe { web_audio_playback_stats_can_update(self.audio_context.parent().global_event_target().as_ptr()) };

        // 5. If canUpdate is false, abort these steps.
        if !can_update {
            return;
        }

        let renderer = self.audio_context.renderer.borrow();
        let Some(renderer) = &*renderer else {
            return;
        };
        let (underrun_duration, underrun_events) = renderer.playback_underruns();

        // 6. Set [[underrun duration]] to the total duration of all underrun events (in seconds) that have occurred in [[audio context]] playback since its construction.
        self.underrun_duration.set(underrun_duration);

        // 7. Set [[underrun events]] to the total number of underrun events that have occurred in [[audio context]] playback since its construction.
        self.underrun_events
            .set(underrun_events.min(u64::from(u32::MAX)) as u32);

        // 8. Set [[total duration]] to [[underrun duration]] + [[audio context]].currentTime.
        self.total_duration
            .set(underrun_duration + self.audio_context.current_time_value());

        if let Some((average, minimum, maximum)) = renderer.playback_latency_statistics() {
            // 9. Set [[average latency]] to the average playback latency (in seconds) of [[audio context]] playback over the currently tracked interval.
            self.average_latency.set(average);

            // 10. Set [[minimum latency]] to the minimum playback latency (in seconds) of [[audio context]] playback over the currently tracked interval.
            self.minimum_latency.set(minimum);

            // 11. Set [[maximum latency]] to the maximum playback latency (in seconds) of [[audio context]] playback over the currently tracked interval.
            self.maximum_latency.set(maximum);
        }
    }

    // https://webaudio.github.io/web-audio-api/#dom-audioplaybackstats-resetlatency
    pub(crate) fn reset_latency(&self, _: &mut CallContext) -> WebIdlResult<()> {
        // 1. Set [[latency reset time]] to currentTime.
        self.latency_reset_time.set(self.audio_context.current_time_value());

        // 2. Let currentLatency be the playback latency of the last frame played by [[audio context]], or 0 if no frames have been played out yet.
        let current_latency = self.audio_context.renderer.borrow().as_ref().map_or(0., |renderer| {
            renderer.reset_playback_latency_statistics(self.latency_reset_time.get());
            renderer.last_played_latency()
        });

        // 3. Set [[average latency]] to currentLatency.
        self.average_latency.set(current_latency);

        // 4. Set [[minimum latency]] to currentLatency.
        self.minimum_latency.set(current_latency);

        // 5. Set [[maximum latency]] to currentLatency.
        self.maximum_latency.set(current_latency);
        Ok(())
    }

    pub(crate) fn underrun_duration(&self, cx: &mut CallContext) -> WebIdlResult<f64> {
        self.refresh(cx.vm());
        Ok(self.underrun_duration.get())
    }

    pub(crate) fn underrun_events(&self, cx: &mut CallContext) -> WebIdlResult<u32> {
        self.refresh(cx.vm());
        Ok(self.underrun_events.get())
    }

    pub(crate) fn total_duration(&self, cx: &mut CallContext) -> WebIdlResult<f64> {
        self.refresh(cx.vm());
        Ok(self.total_duration.get())
    }

    pub(crate) fn average_latency(&self, cx: &mut CallContext) -> WebIdlResult<f64> {
        self.refresh(cx.vm());
        Ok(self.average_latency.get())
    }

    pub(crate) fn minimum_latency(&self, cx: &mut CallContext) -> WebIdlResult<f64> {
        self.refresh(cx.vm());
        Ok(self.minimum_latency.get())
    }

    pub(crate) fn maximum_latency(&self, cx: &mut CallContext) -> WebIdlResult<f64> {
        self.refresh(cx.vm());
        Ok(self.maximum_latency.get())
    }
}

#[unsafe(no_mangle)]
pub(crate) unsafe extern "C" fn web_rust_audio_playback_stats_update(stats: *mut c_void) {
    let stats = unsafe { cell_from_abi::<AudioPlaybackStats>(stats) };
    stats.update_audio_stats();
}
