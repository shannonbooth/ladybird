/*
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 * Copyright (c) 2025, Ben Eidson <b.e.eidson@gmail.com>
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#![allow(clippy::collapsible_if, reason = "preserve the C++ branch structure")]

use std::cell::{Cell, RefCell};
use std::collections::VecDeque;
use std::ffi::c_void;
use std::ptr::NonNull;
use std::rc::Rc;
use std::sync::Arc;

use ak::Utf16String;
use libjs_rust::api::{CellRoot, EmbedderCell, GcRefCell, Trace, Value, Vm};
use libweb_rust::web_audio::rendering::offline_audio_renderer::{OfflineAudioRenderer, OfflineAudioRendererClient};

use super::audio_array::overwrite_float32_array;
use super::audio_buffer::AudioBuffer;
use super::audio_context::{AudioContextRenderSizeCategory, AudioContextRenderSizeHint};
use super::audio_destination_node::AudioDestinationNode;
use super::audio_listener::AudioListener;
use super::base_audio_context::{AnyBaseAudioContext, AudioContextState, BaseAudioContext};
use super::offline_audio_completion_event::OfflineAudioCompletionEvent;
use crate::bindings::conversions::ToJs;
use crate::bindings::cpp_cell::CppCell;
use crate::bindings::event_target::{CppEvent, CppEventTarget, GetTheParent};
use crate::bindings::global_object::window_from_global_object;
use crate::bindings::interface::{
    Inherits, Ref, Wrappable, Wrapped, allocate, with_temporary_execution_context_and_callbacks,
};
use crate::bindings::promise::{WebIdlPromise, promise_realm, reject_promise_with_exception, resolve_promise};
use crate::bindings::{CallContext, Exception, WebIdlResult};

pub(crate) struct OfflineAudioContextOptions {
    pub number_of_channels: u32,
    pub length: Option<u32>,
    pub sample_rate: f32,
    pub render_size_hint: AudioContextRenderSizeHint,
}

// https://webaudio.github.io/web-audio-api/#OfflineAudioContext
#[derive(Trace)]
#[gc(crate = libjs_rust)]
pub(crate) struct OfflineAudioContext {
    context: BaseAudioContext,
    length: Option<u32>,
    number_of_channels: u32,
    rendering_started: Cell<bool>,
    rendered_buffers: GcRefCell<VecDeque<(u64, Ref<AudioBuffer>, WebIdlPromise)>>,
    committed_frames: Cell<u64>,
    rendered_frames: Cell<u64>,
    #[gc(untraced)]
    renderer: RefCell<Option<Arc<OfflineAudioRenderer>>>,

    // https://webaudio.github.io/web-audio-api/#dom-offlineaudiocontext-suspend, keyed by suspend frame.
    suspend_promises: GcRefCell<Vec<(u64, WebIdlPromise)>>,
}

impl Inherits<BaseAudioContext> for OfflineAudioContext {
    fn parent(&self) -> &BaseAudioContext {
        &self.context
    }
}

impl Wrappable for OfflineAudioContext {
    fn cpp_parent(&self) -> Option<NonNull<c_void>> {
        self.context.cpp_parent()
    }

    const FINALIZES: bool = true;

    fn finalize(context: Ref<Self>) {
        context.document_became_inactive();
    }
}

impl GetTheParent for OfflineAudioContext {
    fn get_the_parent(&self) -> Option<CppEventTarget> {
        None
    }
}

struct RendererClient {
    vm: NonNull<Vm>,
    context: CellRoot<EmbedderCell<Wrapped<OfflineAudioContext>>>,
}

impl RendererClient {
    fn vm(&self) -> &Vm {
        // SAFETY: The VM of the control thread outlives its audio contexts.
        unsafe { self.vm.as_ref() }
    }
}

impl OfflineAudioRendererClient for RendererClient {
    fn rendering_suspended(&self, suspend_time: f64) {
        self.context.cell().handle_suspended(self.vm(), suspend_time);
    }

    fn rendering_completed(&self, end_frame: u64, rendered_channels: &[Vec<f32>]) {
        self.context
            .cell()
            .finish_rendering(self.vm(), end_frame, rendered_channels);
    }

    fn sources_ended(&self, nodes: &[u64]) {
        self.context.cell().context.handle_ended_sources(self.vm(), nodes);
    }
}

impl OfflineAudioContext {
    fn this(&self) -> Ref<Self> {
        let AnyBaseAudioContext::OfflineAudioContext(context) = self.context.this() else {
            unreachable!()
        };
        context
    }

    // https://webaudio.github.io/web-audio-api/#dom-offlineaudiocontext-offlineaudiocontext
    pub(crate) fn construct_options(
        cx: &mut CallContext,
        context_options: OfflineAudioContextOptions,
    ) -> WebIdlResult<Ref<Self>> {
        // AD-HOC: This spec text is currently only mentioned in the constructor overload that takes separate arguments,
        //         but these parameters should be validated for both constructors.
        // A NotSupportedError exception MUST be thrown if any of the arguments is negative, zero, or outside its nominal range.
        BaseAudioContext::verify_audio_options_inside_nominal_range(
            context_options.number_of_channels,
            context_options.length.unwrap_or(1),
            context_options.sample_rate,
        )?;
        let window = window_from_global_object(cx.realm().global_object())
            .expect("an OfflineAudioContext is constructed on a Window");
        let global = unsafe { CppCell::from_raw(window.as_event_target().as_ptr()) };

        // Let c be a new OfflineAudioContext object. Initialize c as follows:
        let c = allocate(cx.vm(), |defer_gc| Self {
            context: BaseAudioContext::new(defer_gc, global, context_options.sample_rate),
            length: context_options.length,
            number_of_channels: context_options.number_of_channels,
            rendering_started: Cell::new(false),
            rendered_buffers: GcRefCell::new(VecDeque::new()),
            committed_frames: Cell::new(0),
            rendered_frames: Cell::new(0),
            renderer: RefCell::new(None),
            suspend_promises: GcRefCell::new(Vec::new()),
        });
        c.context
            .initialize(cx.vm(), AnyBaseAudioContext::OfflineAudioContext(c));
        c.context.event_target().set_owner(c);
        // 1. Set the [[control thread state]] for c to "suspended".
        c.context.set_control_state(AudioContextState::Suspended);
        c.context.set_state(AudioContextState::Suspended);

        // 2. Set the [[rendering thread state]] for c to "suspended".
        c.context.set_rendering_state(AudioContextState::Suspended);

        // 3. Set the sampleRate for c, based on the value of contextOptions.sampleRate.
        // NB: BaseAudioContext was created with contextOptions.sampleRate.

        // 4. Determine the [[render quantum size]] for c, based on the value of the contextOptions.renderSizeHint:
        use AudioContextRenderSizeHint::{AudioContextRenderSizeCategory as Category, UnsignedLong};
        // 4.1. If it has the default value of "default" or "hardware", set the [[render quantum size]] private slot to 128.
        if matches!(context_options.render_size_hint, Category(_)) {
            c.context
                .set_render_quantum_size(BaseAudioContext::DEFAULT_RENDER_QUANTUM_SIZE);
        // 4.2. Else, if an integer has been passed, a NotSupportedError MUST be thrown if the value is outside the range specified in § 2.5 Supported Render Quantum Sizes, otherwise set the [[render quantum size]] private slot to the passed value.
        } else if let UnsignedLong(render_quantum_size) = context_options.render_size_hint {
            c.context.verify_render_quantum_size(render_quantum_size)?;
            c.context.set_render_quantum_size(render_quantum_size);
        }

        c.context.listener.set(Some(AudioListener::create(
            cx.vm(),
            AnyBaseAudioContext::OfflineAudioContext(c),
        )));

        // 5. Construct an AudioDestinationNode with its channelCount set to contextOptions.numberOfChannels.
        c.context.destination.set(Some(AudioDestinationNode::create(
            cx,
            AnyBaseAudioContext::OfflineAudioContext(c),
            context_options.number_of_channels,
        )?));

        // FIXME: 6. Let messageChannel be a new MessageChannel.
        // FIXME: 7. Let controlSidePort be the value of messageChannel’s port1 attribute.
        // FIXME: 8. Let renderingSidePort be the value of messageChannel’s port2 attribute.
        // FIXME: 9. Let serializedRenderingSidePort be the result of StructuredSerializeWithTransfer(renderingSidePort, « renderingSidePort »).
        // FIXME: 10. Set this audioWorklet's port to controlSidePort.
        // FIXME: 11. Queue a control message to set the MessagePort on the AudioContextGlobalScope, with serializedRenderingSidePort.
        Ok(c)
    }

    // https://webaudio.github.io/web-audio-api/#dom-offlineaudiocontext-offlineaudiocontext-numberofchannels-length-samplerate
    pub(crate) fn construct_channels(
        cx: &mut CallContext,
        number_of_channels: u32,
        length: u32,
        sample_rate: f32,
    ) -> WebIdlResult<Ref<Self>> {
        Self::construct_options(
            cx,
            OfflineAudioContextOptions {
                number_of_channels,
                length: Some(length),
                sample_rate,
                render_size_hint: AudioContextRenderSizeHint::AudioContextRenderSizeCategory(
                    AudioContextRenderSizeCategory::Default,
                ),
            },
        )
    }

    // https://webaudio.github.io/web-audio-api/#dom-offlineaudiocontext-startrendering
    pub(crate) fn start_rendering(
        &self,
        cx: &mut CallContext,
        promise: WebIdlPromise,
        chunk_size: Option<u32>,
    ) -> WebIdlResult<()> {
        // 1. If this’s relevant global object’s associated Document is not fully active then return a promise rejected with "InvalidStateError" DOMException.
        if !self.context.document_is_fully_active() {
            return Err(Exception::Dom(
                "InvalidStateError",
                Utf16String::from_ascii(b"Document is not fully active"),
            ));
        }

        // AD-HOC: Not in spec explicitly, but this should account for detached iframes too. See /the-offlineaudiocontext-interface/startrendering-after-discard.html WPT.
        if self.context.navigable_is_destroyed() {
            return Err(Exception::Dom(
                "InvalidStateError",
                Utf16String::from_ascii(b"The iframe has been detached"),
            ));
        }

        // 2. Let promise be a new promise.
        // NB: The bindings create the promise.

        // 3. If the [[control thread state]] on the OfflineAudioContext is closed, reject promise with InvalidStateError and abort these steps.
        if self.context.control_state() == AudioContextState::Closed {
            return Err(Exception::Dom(
                "InvalidStateError",
                Utf16String::from_ascii(b"The context is closed"),
            ));
        }

        // 4. If the OfflineAudioContext’s length is not null and [[committed frames]] is greater than or equal to OfflineAudioContext’s length, reject promise with InvalidStateError and abort these steps.
        let committed_frames = self.committed_frames.get();
        if self.length.is_some_and(|length| committed_frames >= u64::from(length)) {
            return Err(Exception::Dom(
                "InvalidStateError",
                Utf16String::from_ascii(b"The context has already committed all its frames"),
            ));
        }

        // 5. Let bufferLength be a number initialized to the value of the chunkSize.
        let mut buffer_length = chunk_size.map(u64::from);
        let render_quantum_size = u64::from(self.context.render_quantum_size_value());

        // 5.1. If the OfflineAudioContext’s length is null:
        if self.length.is_none() {
            // 5.1.1. If chunkSize is null, set bufferLength to render quantum size.
            if chunk_size.is_none() {
                buffer_length = Some(render_quantum_size);
            }
        // 5.2. Otherwise, if the OfflineAudioContext’s length is not null:
        } else if let Some(length) = self.length {
            let remaining = u64::from(length) - committed_frames;
            // 5.2.1. If chunkSize is not null and [[committed frames]] + chunkSize is less than the OfflineAudioContext’s length, set bufferLength to chunkSize.
            if let Some(chunk_size) = chunk_size
                && u64::from(chunk_size) < remaining
            {
                buffer_length = Some(u64::from(chunk_size));
            } else {
                // 5.2.2. Otherwise, set bufferLength to length - [[committed frames]].
                buffer_length = Some(remaining);
            }
        }

        // 6. If bufferLength is not a multiple of render quantum size, round it up to the next multiple of the render quantum size.
        let mut buffer_length = buffer_length.unwrap().div_ceil(render_quantum_size) * render_quantum_size;
        // AD-HOC: Trim the final finite chunk to length, as required by WPT.
        if let Some(length) = self.length {
            buffer_length = buffer_length.min(u64::from(length) - committed_frames);
        }

        // 7. Create a new AudioBuffer buffer, with a number of channels and sample rate equal respectively to the numberOfChannels and sampleRate values passed to this instance’s constructor in the contextOptions parameter; and length equal to bufferLength.
        let buffer_result = u32::try_from(buffer_length)
            .map_err(|_| Exception::range_error("The render buffer is too large"))
            .and_then(|length| {
                self.context
                    .create_buffer(cx, self.number_of_channels, length, self.context.sample_rate_value())
            });

        // 8. If an exception was thrown during the preceding AudioBuffer constructor call, reject promise with this exception and return promise.
        let buffer = match buffer_result {
            Ok(buffer) => buffer,
            Err(error) => {
                reject_promise_with_exception(cx.vm(), promise, error);
                return Ok(());
            }
        };

        // 9. Set [[rendering started]] to true.
        self.rendering_started.set(true);

        // 10. Set [[committed frames]] to the sum of [[committed frames]] and the buffer’s length.
        let end_frame = committed_frames + buffer_length;
        self.committed_frames.set(end_frame);

        // 11. Append buffer to [[rendered buffers]] and begin offline rendering for buffer.
        self.rendered_buffers
            .borrow_mut(cx.vm())
            .push_back((end_frame, buffer, promise));
        self.begin_offline_rendering(cx.vm(), end_frame, buffer_length as usize);

        // 12. Append promise to [[pending promises]].
        self.context.pending_promises.borrow_mut(cx.vm()).push(promise);

        // 13. Return promise.
        Ok(())
    }

    // https://webaudio.github.io/web-audio-api/#begin-offline-rendering
    fn begin_offline_rendering(&self, vm: &Vm, end_frame: u64, length: usize) {
        let mut renderer = self.renderer.borrow_mut();
        let first_render = renderer.is_none();
        let renderer = renderer.get_or_insert_with(|| {
            let renderer = OfflineAudioRenderer::new(
                Arc::clone(self.context.control_message_queue()),
                self.context.destination_value().parent().node_id(),
                self.number_of_channels as usize,
                self.context.sample_rate_value(),
                self.context.render_quantum_size_value() as usize,
            );
            for (frame, _) in &*self.suspend_promises.borrow() {
                renderer.request_suspend(*frame);
            }
            renderer
        });
        if self.rendered_buffers.borrow().len() == 1 {
            renderer.set_client(Some(Rc::new(RendererClient {
                vm: NonNull::from(vm),
                context: CellRoot::new(self.this()),
            })));
        }
        if first_render {
            // AD-HOC: Other engines transition the context to "running" once rendering starts.
            self.context.set_control_state(AudioContextState::Running);
            self.context.set_state(AudioContextState::Running);
            self.context.set_rendering_state(AudioContextState::Running);
            self.queue_a_statechange_event(vm);
            renderer.start_rendering();
        }
        renderer.enqueue_rendering(end_frame, length);
    }

    fn finish_rendering(&self, vm: &Vm, end_frame: u64, rendered_channels: &[Vec<f32>]) {
        let Some((_, buffer, promise)) = self
            .rendered_buffers
            .borrow()
            .iter()
            .find(|entry| entry.0 == end_frame)
            .copied()
        else {
            return;
        };
        let mut cx = CallContext::new(vm, self.context.realm());
        for (channel_index, samples) in rendered_channels.iter().enumerate() {
            let channel_data = AudioBuffer::get_channel_data(&buffer, &mut cx, channel_index as u32)
                .unwrap_or_else(|_| panic!("the rendered channel exists"));
            overwrite_float32_array(&channel_data, samples);
        }

        // 4. Once the rendering is complete, queue a media element task to execute the following steps:
        self.context.queue_a_media_element_task(
            vm,
            (self.this(), promise, buffer, end_frame),
            |vm, (context, promise, buffer, end_frame)| {
                if context
                    .rendered_buffers
                    .borrow()
                    .front()
                    .is_none_or(|entry| entry.0 != end_frame)
                {
                    return;
                }
                let realm = promise_realm(promise);
                with_temporary_execution_context_and_callbacks(realm, || {
                    // 4.1. Resolve the promise created by startRendering() with buffer.
                    resolve_promise(
                        vm,
                        promise,
                        buffer
                            .to_js(&CallContext::new(vm, realm))
                            .expect("an AudioBuffer wraps"),
                    );

                    // 4.2. Remove the promise from [[pending promises]].
                    context
                        .context
                        .pending_promises
                        .borrow_mut(vm)
                        .retain(|pending| *pending != promise);

                    // 4.3. Remove buffer from [[rendered buffers]].
                    context.rendered_buffers.borrow_mut(vm).pop_front();

                    // 4.4. Set [[rendered frames]] to the sum of [[rendered frames]] and the length of buffer.
                    context.rendered_frames.set(end_frame);

                    // 4.5. If length is not null:
                    if let Some(length) = context.length {
                        // 4.5.1. If [[rendered frames]] is greater than or equal to length, Queue a control message to close the OfflineAudioContext.
                        if context.rendered_frames.get() >= u64::from(length)
                            && context.context.control_state() != AudioContextState::Closed
                        {
                            // AD-HOC: Close in this task rather than through a control message, so "closed" is visible
                            //         by the time complete fires.
                            context.context.set_control_state(AudioContextState::Closed);
                            context.run_close_control_message(vm, None);
                        }

                        // 4.5.2. Queue a media element task to fire an event named complete at the OfflineAudioContext using OfflineAudioCompletionEvent whose renderedBuffer property is set to buffer.
                        context
                            .context
                            .queue_a_media_element_task(vm, (context, buffer), |vm, (context, buffer)| {
                                let event = CppEvent::create_for_owner(
                                    context.context.realm(),
                                    &Utf16String::from_ascii(b"complete"),
                                    false,
                                    false,
                                    false,
                                );
                                let event = OfflineAudioCompletionEvent::create(vm, event, buffer);
                                context.context.event_target().dispatch_event(&event.event);
                            });
                    }

                    // NB: Only pending render jobs keep the context rooted through the renderer.
                    if context.rendered_buffers.borrow().is_empty() {
                        context.renderer.borrow().as_ref().unwrap().set_client(None);
                    }
                });
            },
        );
    }

    fn handle_suspended(&self, vm: &Vm, suspend_time: f64) {
        if self.context.control_state() == AudioContextState::Closed {
            return;
        }
        self.context.set_control_state(AudioContextState::Suspended);
        self.context.set_state(AudioContextState::Suspended);
        self.context.set_rendering_state(AudioContextState::Suspended);
        let frame = (suspend_time * f64::from(self.context.sample_rate_value())).round() as u64;

        let suspend_promise = {
            let mut promises = self.suspend_promises.borrow_mut(vm);
            promises
                .iter()
                .position(|entry| entry.0 == frame)
                .map(|index| promises.remove(index).1)
        };
        self.context.queue_a_media_element_task(
            vm,
            (self.this(), suspend_promise),
            |vm, (context, suspend_promise)| {
                with_temporary_execution_context_and_callbacks(context.context.realm(), || {
                    if let Some(promise) = suspend_promise {
                        resolve_promise(vm, promise, Value::UNDEFINED);
                    }
                    context.context.dispatch_statechange();
                });
            },
        );
    }

    fn queue_a_statechange_event(&self, vm: &Vm) {
        self.context.queue_a_media_element_task(vm, self.this(), |_, context| {
            context.context.dispatch_statechange();
        });
    }

    // https://webaudio.github.io/web-audio-api/#dom-offlineaudiocontext-resume
    pub(crate) fn resume(&self, cx: &mut CallContext, promise: WebIdlPromise) -> WebIdlResult<()> {
        // 1. If this’s relevant global object’s associated Document is not fully active then return a promise rejected with "InvalidStateError" DOMException.
        if !self.context.document_is_fully_active() {
            return Err(Exception::Dom(
                "InvalidStateError",
                Utf16String::from_ascii(b"The document is not fully active"),
            ));
        }

        // 2. Let promise be a new Promise.
        // NB: The binding creates promise before calling this method.

        // 3. Abort these steps and reject promise with InvalidStateError when any of following conditions is true:
        //    - The [[control thread state]] on the OfflineAudioContext is closed.
        //    - The [[rendering started]] slot on the OfflineAudioContext is false.
        let control_thread_state = self.context.control_state();
        if control_thread_state == AudioContextState::Closed || !self.rendering_started.get() {
            reject_promise_with_exception(
                cx.vm(),
                promise,
                Exception::Dom(
                    "InvalidStateError",
                    Utf16String::from_ascii(b"The OfflineAudioContext has not started rendering or is closed"),
                ),
            );
            return Ok(());
        }

        // 4. Set the [[control thread state]] flag on the OfflineAudioContext to running.
        self.context.set_control_state(AudioContextState::Running);

        // 5. Queue a control message to resume the OfflineAudioContext.
        let was_suspended = control_thread_state == AudioContextState::Suspended;
        // NB: Wake a suspended renderer in the settlement task so completion cannot overtake the state change.
        self.context.queue_a_media_element_task(
            cx.vm(),
            (self.this(), promise, was_suspended),
            |vm, (context, promise, was_suspended)| {
                // 1. Set the [[rendering thread state]] on the OfflineAudioContext to running.
                // 2. Start rendering the audio graph.
                if was_suspended {
                    context.context.set_rendering_state(AudioContextState::Running);
                    context
                        .renderer
                        .borrow()
                        .as_ref()
                        .expect("rendering has started")
                        .resume();
                }

                // 3. In case of failure, queue a media element task to reject promise and abort the remaining steps.
                // NB: Waking the offline render thread is infallible.

                // 4. queue a media element task to execute the following steps:
                with_temporary_execution_context_and_callbacks(promise_realm(promise), || {
                    // 4.1. Resolve promise.
                    resolve_promise(vm, promise, Value::UNDEFINED);

                    // 4.2. If the state attribute of the OfflineAudioContext is not already "running":
                    // NB: A resume of an already running renderer leaves any later suspension alone.
                    if was_suspended && context.context.state_value() != AudioContextState::Running {
                        // 4.2.1. Set the state attribute of the OfflineAudioContext to "running".
                        context.context.set_state(AudioContextState::Running);

                        // 4.2.2. Queue a media element task to fire an event named statechange at the OfflineAudioContext.
                        context.queue_a_statechange_event(vm);
                    }
                });
            },
        );

        // 6. Return promise.
        Ok(())
    }

    // https://webaudio.github.io/web-audio-api/#dom-offlineaudiocontext-suspend
    pub(crate) fn suspend(&self, cx: &mut CallContext, promise: WebIdlPromise, suspend_time: f64) -> WebIdlResult<()> {
        // Note that the maximum precision of suspension is the size of the render quantum and the specified suspension time will be rounded up to the nearest render quantum boundary.
        let quantum_size = f64::from(self.context.render_quantum_size_value());
        let mut frame = suspend_time * f64::from(self.context.sample_rate_value());
        // NB: Snap to the nearest frame first, so that frame / sampleRate is not rounded up into the next quantum.
        if (frame - frame.round()).abs() < 1. / 2048. {
            frame = frame.round();
        }
        let frame = (frame / quantum_size).ceil() * quantum_size;
        let current_frame = self
            .renderer
            .borrow()
            .as_ref()
            .map_or(0, |renderer| renderer.frames_rendered());
        // AD-HOC: Allow suspending at the first frame before rendering starts, as required by WPT.
        let is_initial_suspension = suspend_time == 0. && !self.rendering_started.get();
        if self.context.control_state() == AudioContextState::Closed
            || (!is_initial_suspension && frame <= current_frame as f64)
            || self.length.is_some_and(|length| frame >= f64::from(length))
            || frame >= u64::MAX as f64
        {
            return Err(Exception::Dom(
                "InvalidStateError",
                Utf16String::from_ascii(b"The quantized suspend time is outside the remaining rendering interval"),
            ));
        }
        let frame = frame as u64;
        if self.suspend_promises.borrow().iter().any(|entry| entry.0 == frame) {
            return Err(Exception::Dom(
                "InvalidStateError",
                Utf16String::from_ascii(b"A suspension is already scheduled at this time"),
            ));
        }
        self.suspend_promises.borrow_mut(cx.vm()).push((frame, promise));
        if let Some(renderer) = &*self.renderer.borrow() {
            if !renderer.request_suspend(frame) {
                self.suspend_promises
                    .borrow_mut(cx.vm())
                    .retain(|entry| entry.0 != frame);
                return Err(Exception::Dom(
                    "InvalidStateError",
                    Utf16String::from_ascii(b"The rendering thread has already passed the requested suspend time"),
                ));
            }
        }
        Ok(())
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

    // https://webaudio.github.io/web-audio-api/#dom-offlineaudiocontext-length
    pub(crate) fn length(&self, _: &mut CallContext) -> WebIdlResult<Option<u32>> {
        Ok(self.length)
    }

    // https://webaudio.github.io/web-audio-api/#dom-offlineaudiocontext-close
    pub(crate) fn close(&self, cx: &mut CallContext, promise: WebIdlPromise) -> WebIdlResult<()> {
        // 1. If this’s relevant global object’s associated Document is not fully active then return a promise rejected with "InvalidStateError" DOMException.
        if !self.context.document_is_fully_active() {
            return Err(Exception::Dom(
                "InvalidStateError",
                Utf16String::from_ascii(b"The document is not fully active"),
            ));
        }

        // 2. Let promise be a new Promise.
        // NB: The bindings create the promise.

        // 3. If the [[control thread state]] flag on the OfflineAudioContext is closed reject the promise with InvalidStateError, abort these steps, returning promise.
        if self.context.control_state() == AudioContextState::Closed {
            return Err(Exception::Dom(
                "InvalidStateError",
                Utf16String::from_ascii(b"The context is closed"),
            ));
        }

        // 4. Set the [[control thread state]] flag on the OfflineAudioContext to closed.
        self.context.set_control_state(AudioContextState::Closed);

        // 5. Queue a control message to close the OfflineAudioContext.
        self.run_close_control_message(cx.vm(), Some(promise));

        // 6. Return promise.
        Ok(())
    }

    // https://webaudio.github.io/web-audio-api/#dom-offlineaudiocontext-close
    fn run_close_control_message(&self, vm: &Vm, promise: Option<WebIdlPromise>) {
        // 1. Set the [[rendering thread state]] to suspended.
        // Note: This will stop rendering.
        self.context.set_rendering_state(AudioContextState::Suspended);
        self.document_became_inactive();

        // 2. If this control message is being run in a reaction to the document being unloaded, abort this algorithm.
        // Note: There is no need to notify the control thread in this case.
        // NB: Document cleanup stops the renderer directly.

        // 3. queue a media element task to execute the following steps:
        let finish = |vm: &Vm, (context, promise): (Ref<Self>, Option<WebIdlPromise>)| {
            with_temporary_execution_context_and_callbacks(context.context.realm(), || {
                // AD-HOC: Cancel outstanding rendering and suspension requests, as required by WPT.
                context.cancel_pending_rendering(vm);
                // 3.1. Resolve promise.
                if let Some(promise) = promise {
                    resolve_promise(vm, promise, Value::UNDEFINED);
                }

                // 3.2. If the state attribute of the OfflineAudioContext is not already "closed":
                if context.context.state_value() != AudioContextState::Closed {
                    // 3.2.1. Set the state attribute of the OfflineAudioContext to "closed".
                    context.context.set_state(AudioContextState::Closed);

                    // 3.2.2. queue a media element task to fire an event named statechange at the OfflineAudioContext.
                    context.queue_a_statechange_event(vm);
                }
            });
        };
        if promise.is_some() {
            self.context
                .queue_a_media_element_task(vm, (self.this(), promise), finish);
        } else {
            // NB: The completed finite render is already settling in a media element task.
            finish(vm, (self.this(), None));
        }
    }

    fn cancel_pending_rendering(&self, vm: &Vm) {
        loop {
            let Some((_, _, promise)) = self.rendered_buffers.borrow().front().copied() else {
                break;
            };
            reject_promise_with_exception(
                vm,
                promise,
                Exception::Dom("AbortError", Utf16String::from_ascii(b"The context was closed")),
            );
            self.context
                .pending_promises
                .borrow_mut(vm)
                .retain(|pending| *pending != promise);
            self.rendered_buffers.borrow_mut(vm).pop_front();
        }
        loop {
            let Some((_, promise)) = self.suspend_promises.borrow().last().copied() else {
                break;
            };
            reject_promise_with_exception(
                vm,
                promise,
                Exception::Dom("InvalidStateError", Utf16String::from_ascii(b"The context was closed")),
            );
            self.suspend_promises.borrow_mut(vm).pop();
        }
    }

    pub(crate) fn document_became_inactive(&self) {
        if let Some(renderer) = &*self.renderer.borrow() {
            renderer.stop();
            renderer.set_client(None);
        }
        self.context.shutdown_audio_worklet();
    }
}
