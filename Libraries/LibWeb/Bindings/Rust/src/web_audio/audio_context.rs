/*
 * Copyright (c) 2023, Luke Wilde <lukew@serenityos.org>
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::cell::{Cell, RefCell};
use std::ffi::c_void;
use std::ptr::NonNull;
use std::rc::Rc;
use std::sync::Arc;

use ak::Utf16String;
use libjs_rust::api::{CellRoot, EmbedderCell, GcRefCell, Trace, Value, Vm};
use libweb_rust::web_audio::control_message::FfiSharedCppObject;
use libweb_rust::web_audio::rendering::node_message::SharedCppObject;
use libweb_rust::web_audio::rendering::realtime_audio_renderer::{RealtimeAudioRenderer, RealtimeAudioRendererClient};

use super::audio_destination_node::AudioDestinationNode;
use super::audio_listener::AudioListener;
use super::audio_node::AudioNodeOptions;
use super::base_audio_context::{AnyBaseAudioContext, AudioContextState, BaseAudioContext};
use super::media_element_audio_source_node::MediaElementAudioSourceNode;
use super::media_stream_audio_destination_node::MediaStreamAudioDestinationNode;
use super::media_stream_audio_source_node::{MediaStreamAudioSourceNode, MediaStreamAudioSourceOptions};
use crate::bindings::conversions::CppInterface;
use crate::bindings::cpp_cell::CppCell;
use crate::bindings::event_target::{CppEventTarget, GetTheParent};
use crate::bindings::global_object::window_from_global_object;
use crate::bindings::interface::with_temporary_execution_context_and_callbacks;
use crate::bindings::interface::{Inherits, Ref, Wrappable, Wrapped, allocate};
use crate::bindings::promise::{
    WebIdlPromise, promise_realm, reject_promise, reject_promise_with_exception, resolve_promise,
};
use crate::bindings::{CallContext, Exception, WebIdlResult};

#[derive(Clone, Copy)]
pub(crate) enum AudioContextLatencyCategory {
    Balanced,
    Interactive,
    Playback,
}

#[allow(dead_code, reason = "the C++ constructor ignores latencyHint")]
pub(crate) enum AudioContextLatencyCategoryOrDouble {
    AudioContextLatencyCategory(AudioContextLatencyCategory),
    Double(f64),
}

#[derive(Clone, Copy)]
pub(crate) enum AudioContextRenderSizeCategory {
    Default,
    Hardware,
}

pub(crate) enum AudioContextRenderSizeHint {
    AudioContextRenderSizeCategory(AudioContextRenderSizeCategory),
    UnsignedLong(u32),
}

pub(crate) struct AudioContextOptions {
    #[allow(dead_code, reason = "the C++ constructor ignores latencyHint")]
    pub latency_hint: AudioContextLatencyCategoryOrDouble,
    pub sample_rate: Option<f32>,
    pub render_size_hint: AudioContextRenderSizeHint,
}

pub(crate) struct AudioTimestamp {
    pub context_time: Option<f64>,
    pub performance_time: Option<f64>,
}

// https://webaudio.github.io/web-audio-api/#AudioContext
#[derive(Trace)]
#[gc(crate = libjs_rust)]
pub(crate) struct AudioContext {
    context: BaseAudioContext,
    allowed_to_start: bool,
    pending_resume_promises: GcRefCell<Vec<WebIdlPromise>>,
    suspended_by_user: Cell<bool>,
    #[gc(untraced)]
    renderer: RefCell<Option<Arc<RealtimeAudioRenderer>>>,
}

impl Inherits<BaseAudioContext> for AudioContext {
    fn parent(&self) -> &BaseAudioContext {
        &self.context
    }
}

impl Wrappable for AudioContext {
    fn cpp_parent(&self) -> Option<NonNull<c_void>> {
        self.context.cpp_parent()
    }

    const FINALIZES: bool = true;

    fn finalize(context: Ref<Self>) {
        context.context.shutdown_audio_worklet();
        if let Some(renderer) = &*context.renderer.borrow() {
            renderer.stop();
            renderer.set_client(None);
        }
    }
}

impl GetTheParent for AudioContext {
    fn get_the_parent(&self) -> Option<CppEventTarget> {
        None
    }
}

unsafe extern "C" {
    fn web_audio_output_factory_create(global: *mut c_void) -> FfiSharedCppObject;
    fn web_audio_realtime_context_performance_time(global: *mut c_void, monotonic_nanoseconds: i64) -> f64;
}

struct RendererClient {
    vm: NonNull<Vm>,
    context: CellRoot<EmbedderCell<Wrapped<AudioContext>>>,
}

impl RealtimeAudioRendererClient for RendererClient {
    fn sources_ended(&self, nodes: &[u64]) {
        // SAFETY: The VM of the control thread outlives its audio contexts.
        let vm = unsafe { self.vm.as_ref() };
        self.context.cell().context.handle_ended_sources(vm, nodes);
    }

    fn output_interrupted(&self) {
        let vm = unsafe { self.vm.as_ref() };
        self.context.cell().interrupt(vm);
    }

    fn output_recovered(&self) {
        let vm = unsafe { self.vm.as_ref() };
        let context = self.context.cell();
        context.context.queue_a_media_element_task(vm, context, |vm, context| {
            with_temporary_execution_context_and_callbacks(context.context.realm(), || context.end_interruption(vm));
        });
    }
}

impl AudioContext {
    fn this(&self) -> Ref<Self> {
        let AnyBaseAudioContext::AudioContext(context) = self.context.this() else {
            unreachable!()
        };
        context
    }

    // https://webaudio.github.io/web-audio-api/#dom-audiocontext-audiocontext
    pub(crate) fn construct(cx: &mut CallContext, context_options: AudioContextOptions) -> WebIdlResult<Ref<Self>> {
        let window =
            window_from_global_object(cx.realm().global_object()).expect("an AudioContext is constructed on a Window");
        let global = unsafe { CppCell::from_raw(window.as_event_target().as_ptr()) };

        // If the current settings object’s responsible document is NOT fully active, throw an InvalidStateError and abort these steps.
        // FIXME: Not all settings objects currently return a responsible document.
        //        Therefore we only fail this check if responsible document is not null.
        unsafe extern "C" {
            fn web_audio_context_responsible_document_is_fully_active(global: *mut c_void) -> bool;
        }
        if !unsafe { web_audio_context_responsible_document_is_fully_active(global.as_ptr()) } {
            return Err(Exception::Dom(
                "InvalidStateError",
                Utf16String::from_ascii(b"Document is not fully active"),
            ));
        }

        // https://webaudio.github.io/web-audio-api/#sample-rates
        // A NotSupportedError MUST be thrown if a sample rate outside this range is specified.
        if let Some(sample_rate) = context_options.sample_rate {
            BaseAudioContext::verify_sample_rate(sample_rate)?;
        }

        // 1. Let context be a new AudioContext object.
        let context = allocate(cx.vm(), |defer_gc| Self {
            context: BaseAudioContext::new(defer_gc, global, 0.),
            allowed_to_start: true,
            pending_resume_promises: GcRefCell::new(Vec::new()),
            suspended_by_user: Cell::new(false),
            renderer: RefCell::new(None),
        });
        context
            .context
            .initialize(cx.vm(), AnyBaseAudioContext::AudioContext(context));
        context.context.event_target().set_owner(context);
        // 2. Set a [[control thread state]] to suspended on context.
        context.context.set_control_state(AudioContextState::Suspended);

        // 3. Set a [[rendering thread state]] to suspended on context.
        context.context.set_rendering_state(AudioContextState::Suspended);

        // 4. Set [[state before interruption]] to null on context.
        // NB: BaseAudioContext initializes this slot.

        // FIXME: 5. Let messageChannel be a new MessageChannel.
        // FIXME: 6. Let controlSidePort be the value of messageChannel’s port1 attribute.
        // FIXME: 7. Let renderingSidePort be the value of messageChannel’s port2 attribute.
        // FIXME: 8. Let serializedRenderingSidePort be the result of StructuredSerializeWithTransfer(renderingSidePort, « renderingSidePort »).
        // FIXME: 9. Set this audioWorklet’s port to controlSidePort.
        // FIXME: 10. Queue a control message to set the MessagePort on the AudioContextGlobalScope, with serializedRenderingSidePort.
        // 11. If contextOptions is given, perform the following substeps:
        // FIXME: 11.1. If sinkId is specified, let sinkId be the value of contextOptions.sinkId and run the following substeps:
        // FIXME: 11.1.1. If both sinkId and [[sink ID]] are a type of DOMString, and they are equal to each other, abort these substeps.
        // FIXME: 11.1.2. If sinkId is a type of AudioSinkOptions and [[sink ID]] is a type of AudioSinkInfo, and type in sinkId and type in [[sink ID]] are equal, abort these substeps.
        // FIXME: 11.1.3. If sinkId is a type of DOMString, set [[sink ID at construction]] to sinkId and abort these substeps.
        // FIXME: 11.1.4. If sinkId is a type of AudioSinkOptions, set [[sink ID at construction]] to a new instance of AudioSinkInfo created with the value of type of sinkId.
        // FIXME: 11.2. Set the internal latency of context according to contextOptions.latencyHint, as described in latencyHint.
        // 11.3. If contextOptions.sampleRate is specified, set the sampleRate of context to this value. Otherwise, follow these substeps:
        // If contextOptions.sampleRate differs from the sample rate of the output device, the user agent MUST resample the audio output to match the sample rate of the output device.
        // Note: If resampling is required, the latency of context may be affected, possibly by a large amount.
        if let Some(sample_rate) = context_options.sample_rate {
            context.context.set_sample_rate(sample_rate);
        } else {
            // FIXME: 11.3.1. If sinkId is the empty string or a type of AudioSinkOptions, use the sample rate of the default output device. Abort these substeps.
            // FIXME: 11.3.2. If sinkId is a DOMString, use the sample rate of the output device identified by sinkId. Abort these substeps.
            context.context.set_sample_rate(44100.);
        }

        // AD-HOC: Ensure a sample rate is always set, even when no options were passed at all.
        if context.context.sample_rate_value() == 0. {
            context.context.set_sample_rate(44100.);
        }

        // 11.4. Set the [[render quantum size]] of context based on the value of the contextOptions.renderSizeHint:
        use AudioContextRenderSizeHint::{AudioContextRenderSizeCategory as Category, UnsignedLong};
        // 11.4.1. If it has the default value of "default", set the [[render quantum size]] private slot to 128.
        if matches!(
            context_options.render_size_hint,
            Category(AudioContextRenderSizeCategory::Default)
        ) {
            context
                .context
                .set_render_quantum_size(BaseAudioContext::DEFAULT_RENDER_QUANTUM_SIZE);
        // 11.4.2. Else, if it has the value of "hardware", set the [[render quantum size]] private slot to 0.
        } else if matches!(
            context_options.render_size_hint,
            Category(AudioContextRenderSizeCategory::Hardware)
        ) {
            // AD-HOC: The output always uses the default size, so report it before the context runs, as required by WPT.
            context
                .context
                .set_render_quantum_size(BaseAudioContext::DEFAULT_RENDER_QUANTUM_SIZE);
        // 11.4.3. Else, if an integer has been passed, a NotSupportedError MUST be thrown if the value is outside the range specified in § 2.5 Supported Render Quantum Sizes, otherwise set the [[render quantum size]] private slot to the passed value.
        } else if let UnsignedLong(render_quantum_size) = context_options.render_size_hint {
            context.context.verify_render_quantum_size(render_quantum_size)?;
            context.context.set_render_quantum_size(render_quantum_size);
        }

        context.context.listener.set(Some(AudioListener::create(
            cx.vm(),
            AnyBaseAudioContext::AudioContext(context),
        )));
        context.context.destination.set(Some(AudioDestinationNode::create(
            cx,
            AnyBaseAudioContext::AudioContext(context),
            2,
        )?));

        // 12. If context is allowed to start, send a control message to start processing.
        // FIXME: The context is currently always allowed to start; this should be gated on sticky activation.
        if context.allowed_to_start {
            // FIXME: 1. Let validationResult be the return value of sink identifier validation of [[sink ID at construction]].
            // FIXME: 2. If validationResult is false, execute the following steps:
            // FIXME: 2.1. Set [[sink ID]] to the empty string.
            // FIXME: 2.2. Queue a media element task to fire an event named error at the AudioContext, and abort the following steps.
            // 3. Attempt to acquire system resources to use a following audio output device based on [[sink ID at construction]] for rendering:
            // 4. The default audio output device for the empty string.
            // FIXME: 5. An audio output device identified by [[sink ID at construction]].
            // FIXME: 5.1. If resource acquisition fails, queue a media element task to fire an event named error at the AudioContext, and abort the following steps.
            // FIXME: 6. Set [[sink ID]] to the value of [[sink ID at construction]].
            let factory = unsafe { web_audio_output_factory_create(context.context.global_event_target().as_ptr()) };
            let renderer = RealtimeAudioRenderer::new(
                Arc::clone(context.context.control_message_queue()),
                context.context.destination_value().parent().node_id(),
                context.context.sample_rate_value(),
                context.context.render_quantum_size_value() as usize,
                SharedCppObject {
                    pointer: factory.pointer,
                    retain: factory.retain,
                    release: factory.release,
                },
            );
            *context.renderer.borrow_mut() = Some(Arc::clone(&renderer));
            context.set_renderer_client(cx.vm());
            renderer.start_rendering();

            // 7. Set this [[rendering thread state]] to running on the AudioContext.
            context.context.set_rendering_state(AudioContextState::Running);

            // 8. Queue a media element task to execute the following steps:
            context
                .context
                .queue_a_media_element_task(cx.vm(), context, |_, context| {
                    // 8.1. If the [[render quantum size]] of the AudioContext is 0, set it to the actual hardware render quantum size chosen during resource acquisition.
                    if context.context.render_quantum_size.get() == 0 {
                        context
                            .context
                            .set_render_quantum_size(context.context.render_quantum_size_value());
                    }

                    // 8.2. Set the state attribute of the AudioContext to "running".
                    context.context.set_state(AudioContextState::Running);

                    // 8.3. fire an event named statechange at the AudioContext.
                    context.context.dispatch_statechange();
                });
        }

        // 12. Return context.
        Ok(context)
    }

    pub(crate) fn document_became_inactive(&self) {
        // Release the audio output device and the GC roots held by the renderer's callbacks, so a context belonging to a
        // navigated-away document stops playing and can eventually be collected.
        self.context.shutdown_audio_worklet();
        if let Some(renderer) = &*self.renderer.borrow() {
            renderer.stop();
            renderer.set_client(None);
        }
    }

    // https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-currenttime
    pub(crate) fn current_time_value(&self) -> f64 {
        // This is the time in seconds of the sample frame immediately following the last sample-frame in the block of
        // audio most recently processed by the context's rendering graph.
        if let Some(renderer) = &*self.renderer.borrow() {
            return renderer.frames_rendered() as f64 / f64::from(self.context.sample_rate_value());
        }
        0.
    }

    // https://webaudio.github.io/web-audio-api/#dom-audiocontext-baselatency
    pub(crate) fn base_latency(&self, _: &mut CallContext) -> WebIdlResult<f64> {
        Ok(f64::from(self.context.render_quantum_size_value()) / f64::from(self.context.sample_rate_value()))
    }

    // https://webaudio.github.io/web-audio-api/#dom-audiocontext-outputlatency
    pub(crate) fn output_latency(&self, _: &mut CallContext) -> WebIdlResult<f64> {
        Ok(self
            .renderer
            .borrow()
            .as_ref()
            .map_or(0., |renderer| renderer.output_latency()))
    }

    // https://webaudio.github.io/web-audio-api/#dom-audiocontext-getoutputtimestamp
    pub(crate) fn get_output_timestamp(&self, _: &mut CallContext) -> WebIdlResult<AudioTimestamp> {
        // If the context's rendering graph has not yet processed a block of audio, then
        // getOutputTimestamp call returns an AudioTimestamp instance with both members
        // containing zero.
        let Some((context_time, monotonic_time)) = self
            .renderer
            .borrow()
            .as_ref()
            .and_then(|renderer| renderer.output_timestamp())
        else {
            return Ok(AudioTimestamp {
                context_time: Some(0.),
                performance_time: Some(0.),
            });
        };

        Ok(AudioTimestamp {
            context_time: Some(context_time),
            performance_time: Some(unsafe {
                web_audio_realtime_context_performance_time(self.context.global_event_target().as_ptr(), monotonic_time)
            }),
        })
    }

    // https://webaudio.github.io/web-audio-api/#dom-audiocontext-interruption-start
    fn interrupt(&self, vm: &Vm) {
        // 1. If the context’s [[rendering thread state]] is closed or interrupted, abort these steps.
        let rendering_thread_state = self.context.rendering_state();
        if self.context.control_state() == AudioContextState::Closed
            || matches!(
                rendering_thread_state,
                AudioContextState::Closed | AudioContextState::Interrupted
            )
        {
            return;
        }

        // 2. If the context’s [[rendering thread state]] is running:
        if rendering_thread_state == AudioContextState::Running {
            // 2.1. Attempt to release system resources.
            // NB: The renderer delivers this notification after losing its stream.

            // 2.2. Queue a media element task to execute the following steps:
            self.context.queue_a_media_element_task(vm, self.this(), |_, context| {
                if context.context.control_state() == AudioContextState::Closed {
                    return;
                }

                // 2.2.1. Set the context’s [[control thread state]] to interrupted.
                context.context.set_control_state(AudioContextState::Interrupted);
                context.context.set_state(AudioContextState::Interrupted);

                // 2.2.2. Set the context’s [[state before interruption]] slot to "running".
                context
                    .context
                    .state_before_interruption
                    .set(Some(AudioContextState::Running));

                // 2.2.3. Fire an event named statechange at the context.
                context.context.dispatch_statechange();
            });
        }

        // 3. If the context’s [[rendering thread state]] is suspended:
        if rendering_thread_state == AudioContextState::Suspended {
            // 3.1. Queue a media element task to execute the following steps:
            self.context.queue_a_media_element_task(vm, self.this(), |_, context| {
                if context.context.control_state() == AudioContextState::Closed {
                    return;
                }
                // 3.1.1. Set the context’s [[control thread state]] to interrupted.
                context.context.set_control_state(AudioContextState::Interrupted);

                // 3.1.2. Set the context’s [[state before interruption]] slot to "suspended".
                context
                    .context
                    .state_before_interruption
                    .set(Some(AudioContextState::Suspended));
            });
        }

        // 4. Set the context’s [[rendering thread state]] to interrupted.
        self.context.set_rendering_state(AudioContextState::Interrupted);
    }

    // https://webaudio.github.io/web-audio-api/#dom-audiocontext-interruption-end
    fn end_interruption(&self, vm: &Vm) {
        // 1. If the context’s [[rendering thread state]] is not interrupted, abort these steps.
        if self.context.rendering_state() != AudioContextState::Interrupted
            || self.context.control_state() == AudioContextState::Closed
        {
            return;
        }

        // NB: Another stream loss can arrive before this recovery task runs.
        if self
            .renderer
            .borrow()
            .as_ref()
            .is_some_and(|renderer| renderer.output_is_interrupted())
        {
            return;
        }

        let state_before_interruption = self.context.state_before_interruption.get();
        // 2. If the context’s [[state before interruption]] is "running":
        if state_before_interruption == Some(AudioContextState::Running) {
            // 2.1. Attempt to acquire system resources.
            // NB: The renderer delivers this notification once the replacement stream has opened.

            // 2.2. Set the [[rendering thread state]] on the AudioContext to "running".
            self.context.set_rendering_state(AudioContextState::Running);

            // 2.3. Start rendering the audio graph.
            // NB: Installing the replacement stream restarts rendering.

            // 2.4. Queue a media element task to execute the following steps:
            // NB: The callback already runs in a media element task.
            self.resolve_pending_resume_promises(vm);

            // 2.4.1. If the state attribute of the AudioContext is not already "running":
            if self.context.state_value() != AudioContextState::Running {
                // 2.4.1.1. Set the context’s [[control thread state]] to "running".
                self.context.set_control_state(AudioContextState::Running);
                self.context.set_state(AudioContextState::Running);

                // 2.4.1.2. Fire an event named statechange at the context.
                self.context.dispatch_statechange();
            }
        }

        // 3. If the context’s [[state before interruption]] is "suspended"
        if state_before_interruption == Some(AudioContextState::Suspended) {
            // 3.1. Set the [[rendering thread state]] on the AudioContext to suspended.
            self.context.set_rendering_state(AudioContextState::Suspended);

            // 3.2. Queue a media element task to execute the following steps:
            // NB: The callback already runs in a media element task.
            // 3.2.1. Set the context’s [[control thread state]] to suspended.
            self.context.set_control_state(AudioContextState::Suspended);
        }

        // 4. Set the context’s [[state before interruption]] to null.
        self.context.state_before_interruption.set(None);
    }

    // https://webaudio.github.io/web-audio-api/#dom-audiocontext-resume
    pub(crate) fn resume(&self, cx: &mut CallContext, promise: WebIdlPromise) -> WebIdlResult<()> {
        // 1. If this’s relevant global object’s associated Document is not fully active then return a promise rejected with "InvalidStateError" DOMException.
        if !self.context.document_is_fully_active() {
            return Err(Exception::Dom(
                "InvalidStateError",
                Utf16String::from_ascii(b"Document is not fully active"),
            ));
        }

        // 2. Let promise be a new Promise.
        // NB: The bindings create the promise.

        // 3. If the [[control thread state]] on the AudioContext is closed reject the promise with InvalidStateError, abort these steps, returning promise.
        if self.context.control_state() == AudioContextState::Closed {
            reject_promise_with_exception(
                cx.vm(),
                promise,
                Exception::Dom(
                    "InvalidStateError",
                    Utf16String::from_ascii(b"Audio context is already closed."),
                ),
            );
            return Ok(());
        }

        // 4. Set [[suspended by user]] to false.
        self.suspended_by_user.set(false);

        // NB: While the context is suspended the renderer can lose its device without a client.
        let output_is_interrupted = self
            .renderer
            .borrow()
            .as_ref()
            .is_some_and(|renderer| renderer.output_is_interrupted());
        if self.context.state_value() == AudioContextState::Suspended {
            if output_is_interrupted {
                self.context.set_control_state(AudioContextState::Interrupted);
                self.context.set_rendering_state(AudioContextState::Interrupted);
            } else if self.context.control_state() == AudioContextState::Interrupted {
                self.context.set_control_state(AudioContextState::Suspended);
            }
        }

        // 5. If the context’s state attribute is "suspended" and the context’s [[control thread state]] is "interrupted", then:
        if self.context.state_value() == AudioContextState::Suspended
            && self.context.control_state() == AudioContextState::Interrupted
        {
            self.set_renderer_client(cx.vm());
            self.start_rendering_audio_graph();

            // 5.1. Queue a media element task to execute the following steps:
            self.context
                .queue_a_media_element_task(cx.vm(), self.this(), |vm, context| {
                    if context.context.control_state() == AudioContextState::Closed {
                        return;
                    }

                    // 5.1.1. Set the state attribute of the AudioContext to "interrupted".
                    context.context.set_state(AudioContextState::Interrupted);

                    // 5.1.2. Set the [[state before interruption]] slot to "running".
                    context
                        .context
                        .state_before_interruption
                        .set(Some(AudioContextState::Running));

                    // 5.1.3. Queue a media element task to fire an event named statechange at the AudioContext.
                    context
                        .context
                        .queue_a_media_element_task(vm, context, |_, context| context.context.dispatch_statechange());
                });

            // 5.2. Reject the promise with InvalidStateError, abort these steps, returning promise.
            return Err(Exception::Dom(
                "InvalidStateError",
                Utf16String::from_ascii(b"The audio output is interrupted"),
            ));
        }

        // 6. If the context is not allowed to start, append promise to [[pending promises]] and [[pending resume promises]] and abort these steps, returning promise.
        if !self.allowed_to_start {
            self.context.pending_promises.borrow_mut(cx.vm()).push(promise);
            self.pending_resume_promises.borrow_mut(cx.vm()).push(promise);
            return Ok(());
        }

        // 7. Set the [[control thread state]] on the AudioContext to running.
        self.context.set_control_state(AudioContextState::Running);

        // 8. Queue a control message to resume the AudioContext with promise.
        self.run_control_message_to_resume(cx.vm(), promise);

        // 9. Return promise.
        Ok(())
    }

    fn run_control_message_to_resume(&self, vm: &Vm, promise: WebIdlPromise) {
        // 1. Let promise be the promise passed into this algorithm.

        // 2. Attempt to acquire system resources.
        // NB: The renderer acquires resources when starting the graph in step 4.
        if self.renderer.borrow().is_some() {
            self.set_renderer_client(vm);
        }

        // NB: A replacement stream is still acquiring resources.
        if self
            .renderer
            .borrow()
            .as_ref()
            .is_some_and(|renderer| renderer.output_is_interrupted())
        {
            self.context.pending_promises.borrow_mut(vm).push(promise);
            self.pending_resume_promises.borrow_mut(vm).push(promise);
            // AD-HOC: Resume once the interruption ends, even if a suspend has already left it.
            self.context.set_rendering_state(AudioContextState::Interrupted);
            self.context.queue_a_media_element_task(vm, self.this(), |_, context| {
                context
                    .context
                    .state_before_interruption
                    .set(Some(AudioContextState::Running));
            });
            self.start_rendering_audio_graph();
            return;
        }

        // 3. Set the [[rendering thread state]] on the AudioContext to running.
        self.context.set_rendering_state(AudioContextState::Running);

        // 4. Start rendering the audio graph.
        let started = self.start_rendering_audio_graph();

        // 5. In case of failure, queue a media element task to execute the following steps:
        if !started {
            self.context.queue_a_media_element_task(vm, self.this(), |vm, context| {
                // 5.1. Reject all promises from [[pending resume promises]] in order, then clear [[pending resume promises]].
                for promise in &*context.pending_resume_promises.borrow() {
                    with_temporary_execution_context_and_callbacks(promise_realm(*promise), || {
                        reject_promise(vm, *promise, Value::NULL);
                    });

                    // 5.2. Additionally, remove those promises from [[pending promises]].
                    let mut pending = context.context.pending_promises.borrow_mut(vm);
                    if let Some(index) = pending.iter().position(|pending| pending == promise) {
                        pending.remove(index);
                    }
                }
                context.pending_resume_promises.borrow_mut(vm).clear();
            });
        }

        // 6. queue a media element task to execute the following steps:
        self.context
            .queue_a_media_element_task(vm, (self.this(), promise), |vm, (context, promise)| {
                // 6.1. If the [[render quantum size]] of the AudioContext is 0, set it to the actual hardware render quantum size chosen during resource acquisition.
                if context.context.render_quantum_size.get() == 0 {
                    context
                        .context
                        .set_render_quantum_size(context.context.render_quantum_size_value());
                }

                // 6.2. Resolve all promises from [[pending resume promises]] in order.
                // 6.3. Clear [[pending resume promises]]. Additionally, remove those promises from [[pending promises]].
                with_temporary_execution_context_and_callbacks(promise_realm(promise), || {
                    context.resolve_pending_resume_promises(vm);
                    // 6.4. Resolve promise.
                    resolve_promise(vm, promise, Value::UNDEFINED);

                    // 6.5. If the state attribute of the AudioContext is not already "running":
                    if context.context.state_value() != AudioContextState::Running {
                        // 6.5.1. Set the state attribute of the AudioContext to "running".
                        context.context.set_state(AudioContextState::Running);

                        // 6.5.2. Queue a media element task to fire an event named statechange at the AudioContext.
                        context.context.queue_a_media_element_task(vm, context, |_, context| {
                            context.context.dispatch_statechange();
                        });
                    }
                });
            });
    }

    fn resolve_pending_resume_promises(&self, vm: &Vm) {
        for promise in &*self.pending_resume_promises.borrow() {
            with_temporary_execution_context_and_callbacks(promise_realm(*promise), || {
                resolve_promise(vm, *promise, Value::UNDEFINED);
            });
            self.context
                .pending_promises
                .borrow_mut(vm)
                .retain(|pending| pending != promise);
        }
        self.pending_resume_promises.borrow_mut(vm).clear();
    }

    fn reject_pending_resume_promises(&self, vm: &Vm) {
        with_temporary_execution_context_and_callbacks(self.context.realm(), || {
            for pending in &*self.pending_resume_promises.borrow() {
                with_temporary_execution_context_and_callbacks(promise_realm(*pending), || {
                    reject_promise_with_exception(
                        vm,
                        *pending,
                        Exception::Dom(
                            "InvalidStateError",
                            Utf16String::from_ascii(b"Audio output could not be resumed"),
                        ),
                    );
                });
                self.context
                    .pending_promises
                    .borrow_mut(vm)
                    .retain(|promise| promise != pending);
            }
            self.pending_resume_promises.borrow_mut(vm).clear();
        });
    }

    // https://webaudio.github.io/web-audio-api/#dom-audiocontext-suspend
    pub(crate) fn suspend(&self, cx: &mut CallContext, promise: WebIdlPromise) -> WebIdlResult<()> {
        // 1. If this’s relevant global object’s associated Document is not fully active then return a promise rejected with "InvalidStateError" DOMException.
        if !self.context.document_is_fully_active() {
            return Err(Exception::Dom(
                "InvalidStateError",
                Utf16String::from_ascii(b"Document is not fully active"),
            ));
        }

        // 2. Let promise be a new Promise.
        // NB: The bindings create the promise.

        // 3. If the [[control thread state]] on the AudioContext is closed reject the promise with InvalidStateError, abort these steps, returning promise.
        if self.context.control_state() == AudioContextState::Closed {
            reject_promise_with_exception(
                cx.vm(),
                promise,
                Exception::Dom(
                    "InvalidStateError",
                    Utf16String::from_ascii(b"Audio context is already closed."),
                ),
            );
            return Ok(());
        }

        // 4. Append promise to [[pending promises]].
        self.context.pending_promises.borrow_mut(cx.vm()).push(promise);

        // 5. Set [[suspended by user]] to true.
        self.suspended_by_user.set(true);

        // 6. Set the [[control thread state]] on the AudioContext to suspended.
        self.context.set_control_state(AudioContextState::Suspended);

        // 7. Queue a control message to suspend the AudioContext with promise.
        self.run_control_message_to_suspend(cx.vm(), promise);

        // 8. Return promise.
        Ok(())
    }

    fn run_control_message_to_suspend(&self, vm: &Vm, promise: WebIdlPromise) {
        // 1. Let promise be the promise passed into this algorithm.

        // 2. Attempt to release system resources.
        if let Some(renderer) = &*self.renderer.borrow() {
            renderer.suspend();
            renderer.set_client(None);
        }

        // 3. If the [[rendering thread state]] on the AudioContext is "interrupted", queue a media element task to set the [[state before interruption]] slot to "suspended".
        if self.context.rendering_state() == AudioContextState::Interrupted {
            // AD-HOC: Resume promises that were waiting for the interruption to end can no longer be resolved. Reject
            //         them now so that a later resume() keeps its promise.
            self.reject_pending_resume_promises(vm);
            self.context.queue_a_media_element_task(vm, self.this(), |_, context| {
                context
                    .context
                    .state_before_interruption
                    .set(Some(AudioContextState::Suspended));
            });
        }

        // 4. Set the [[rendering thread state]] on the AudioContext to "suspended".
        self.context.set_rendering_state(AudioContextState::Suspended);

        // 5. Queue a media element task to execute the following steps:
        self.context
            .queue_a_media_element_task(vm, (self.this(), promise), |vm, (context, promise)| {
                with_temporary_execution_context_and_callbacks(promise_realm(promise), || {
                    // 5.1. Resolve promise.
                    resolve_promise(vm, promise, Value::UNDEFINED);

                    // AD-HOC: The spec omits removing a settled suspension promise from [[pending promises]].
                    context
                        .context
                        .pending_promises
                        .borrow_mut(vm)
                        .retain(|pending| *pending != promise);

                    // 5.2. If the state attribute of the AudioContext is not already "suspended":
                    if context.context.state_value() != AudioContextState::Suspended {
                        // 5.2.1. Set the state attribute of the AudioContext to "suspended".
                        context.context.set_state(AudioContextState::Suspended);

                        // 5.2.2. Queue a media element task to fire an event named statechange at the AudioContext.
                        context.context.queue_a_media_element_task(vm, context, |_, context| {
                            context.context.dispatch_statechange();
                        });
                    }
                });
            });
    }

    // https://webaudio.github.io/web-audio-api/#dom-audiocontext-close
    pub(crate) fn close(&self, cx: &mut CallContext, promise: WebIdlPromise) -> WebIdlResult<()> {
        // 1. If this’s relevant global object’s associated Document is not fully active then return a promise rejected with "InvalidStateError" DOMException.
        if !self.context.document_is_fully_active() {
            return Err(Exception::Dom(
                "InvalidStateError",
                Utf16String::from_ascii(b"Document is not fully active"),
            ));
        }

        // 2. Let promise be a new Promise.
        // NB: The bindings create the promise.

        // 3. If the [[control thread state]] flag on the AudioContext is closed reject the promise with InvalidStateError, abort these steps, returning promise.
        if self.context.control_state() == AudioContextState::Closed {
            reject_promise_with_exception(
                cx.vm(),
                promise,
                Exception::Dom(
                    "InvalidStateError",
                    Utf16String::from_ascii(b"Audio context is already closed."),
                ),
            );
            return Ok(());
        }

        // 4. Set the [[control thread state]] flag on the AudioContext to closed.
        self.context.set_control_state(AudioContextState::Closed);

        // 5. Queue a control message to close the AudioContext.
        self.run_control_message_to_close(cx.vm(), promise);

        // 6. Return promise.
        Ok(())
    }

    fn run_control_message_to_close(&self, vm: &Vm, promise: WebIdlPromise) {
        // 1. Attempt to release system resources.
        self.context.shutdown_audio_worklet();
        if let Some(renderer) = &*self.renderer.borrow() {
            renderer.stop();
            renderer.set_client(None);
        }

        // 2. Set the [[rendering thread state]] to suspended.
        // NOTE: This will stop rendering.
        self.context.set_rendering_state(AudioContextState::Suspended);

        // 3. If this control message is being run in a reaction to the document being unloaded, abort this algorithm.
        // NOTE: There is no need to notify the control thread in this case.
        // NB: Document teardown releases the renderer directly.
        // 4. queue a media element task to execute the following steps:
        self.context
            .queue_a_media_element_task(vm, (self.this(), promise), |vm, (context, promise)| {
                with_temporary_execution_context_and_callbacks(promise_realm(promise), || {
                    // NB: Resource acquisition cannot complete after close.
                    context.reject_pending_resume_promises(vm);

                    // 4.1. Resolve promise.
                    resolve_promise(vm, promise, Value::UNDEFINED);

                    // 4.2. If the state attribute of the AudioContext is not already "closed":
                    if context.context.state_value() != AudioContextState::Closed {
                        // 4.2.1. Set the state attribute of the AudioContext to "closed".
                        context.context.set_state(AudioContextState::Closed);

                        // 4.2.2. queue a media element task to fire an event named statechange at the AudioContext.
                        context.context.queue_a_media_element_task(vm, context, |_, context| {
                            context.context.dispatch_statechange();
                        });
                    }
                });
            });
    }

    fn start_rendering_audio_graph(&self) -> bool {
        if let Some(renderer) = &*self.renderer.borrow() {
            renderer.resume();
        }
        true
    }

    // The renderer's callback captures a GC root on this context, keeping it alive while it renders. That root is released
    // by clearing the callbacks when the context is suspended, closed, or its document becomes inactive, so this is called
    // again to re-establish it whenever rendering resumes.
    fn set_renderer_client(&self, vm: &Vm) {
        self.renderer
            .borrow()
            .as_ref()
            .unwrap()
            .set_client(Some(Rc::new(RendererClient {
                vm: NonNull::from(vm),
                context: CellRoot::new(self.this()),
            })));
    }

    // https://webaudio.github.io/web-audio-api/#dom-audiocontext-createmediaelementsource
    pub(crate) fn create_media_element_source(
        &self,
        cx: &mut CallContext,
        media_element: CppInterface,
    ) -> WebIdlResult<Ref<MediaElementAudioSourceNode>> {
        MediaElementAudioSourceNode::create(cx, self.this(), media_element)
    }

    // https://webaudio.github.io/web-audio-api/#dom-audiocontext-createmediastreamsource
    pub(crate) fn create_media_stream_source(
        &self,
        cx: &mut CallContext,
        media_stream: CppInterface,
    ) -> WebIdlResult<Ref<MediaStreamAudioSourceNode>> {
        MediaStreamAudioSourceNode::create(cx, self.this(), &MediaStreamAudioSourceOptions { media_stream })
    }

    // https://webaudio.github.io/web-audio-api/#dom-audiocontext-createmediastreamdestination
    pub(crate) fn create_media_stream_destination(
        &self,
        cx: &mut CallContext,
    ) -> WebIdlResult<Ref<MediaStreamAudioDestinationNode>> {
        MediaStreamAudioDestinationNode::create(cx, self.this(), &AudioNodeOptions::default())
    }
}
