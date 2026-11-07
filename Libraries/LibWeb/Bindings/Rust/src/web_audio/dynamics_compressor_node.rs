/*
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;
use std::ptr::NonNull;
use std::sync::Arc;
use std::sync::atomic::{AtomicU32, Ordering};

use ak::Utf16String;
use libjs_rust::api::Trace;
use libweb_rust::web_audio::control_message::SetChannelConfig;
use libweb_rust::web_audio::rendering::dynamics_compressor_render_node::{
    DynamicsCompressorParams, create_dynamics_compressor_render_node, initial_automatic_makeup_gain,
};

use super::audio_node::{
    AnyAudioNode, AudioNode, AudioNodeDefaultOptions, AudioNodeOptions, ChannelCountMode, ChannelInterpretation,
};
use super::audio_param::{AudioParam, AutomationRate, FixedAutomationRate, ParameterType};
use super::base_audio_context::AnyBaseAudioContext;
use crate::bindings::event_target::{CppEventTarget, GetTheParent};
use crate::bindings::interface::{Inherits, Ref, Wrappable, allocate};
use crate::bindings::{CallContext, Exception, WebIdlResult};

// https://webaudio.github.io/web-audio-api/#DynamicsCompressorNode
#[derive(Trace)]
#[gc(crate = libjs_rust)]
pub(crate) struct DynamicsCompressorNode {
    node: AudioNode,

    // https://webaudio.github.io/web-audio-api/#dom-dynamicscompressornode-threshold
    threshold: Ref<AudioParam>,

    // https://webaudio.github.io/web-audio-api/#dom-dynamicscompressornode-knee
    knee: Ref<AudioParam>,

    // https://webaudio.github.io/web-audio-api/#dom-dynamicscompressornode-ratio
    ratio: Ref<AudioParam>,

    // https://webaudio.github.io/web-audio-api/#dom-dynamicscompressornode-attack
    attack: Ref<AudioParam>,

    // https://webaudio.github.io/web-audio-api/#dom-dynamicscompressornode-release
    release: Ref<AudioParam>,

    // https://webaudio.github.io/web-audio-api/#dom-dynamicscompressornode-internal-reduction-slot
    #[gc(untraced)]
    internal_reduction: Arc<AtomicU32>,
    pub(super) makeup_gain: Ref<AudioParam>,
    pub(super) automatic_makeup_gain: bool,
}

impl Inherits<AudioNode> for DynamicsCompressorNode {
    fn parent(&self) -> &AudioNode {
        &self.node
    }
}

impl Wrappable for DynamicsCompressorNode {
    fn cpp_parent(&self) -> Option<NonNull<c_void>> {
        self.node.cpp_parent()
    }

    const FINALIZES: bool = true;

    fn finalize(node: Ref<Self>) {
        node.node.finalize_node();
    }
}

impl GetTheParent for DynamicsCompressorNode {
    fn get_the_parent(&self) -> Option<CppEventTarget> {
        None
    }
}

pub(crate) enum MakeupGainOptions {
    Automatic,
}

pub(crate) enum FloatOrMakeupGainOptions {
    Float(f32),
    MakeupGainOptions(MakeupGainOptions),
}

pub(crate) struct DynamicsCompressorOptions {
    pub parent: AudioNodeOptions,
    pub threshold: f32,
    pub knee: f32,
    pub ratio: f32,
    pub attack: f32,
    pub release: f32,
    pub makeup_gain: FloatOrMakeupGainOptions,
}

impl DynamicsCompressorNode {
    // https://webaudio.github.io/web-audio-api/#dom-dynamicscompressornode-dynamicscompressornode
    pub(crate) fn construct(
        cx: &mut CallContext,
        context: AnyBaseAudioContext,
        options: DynamicsCompressorOptions,
    ) -> WebIdlResult<Ref<Self>> {
        Self::create(cx, context, &options)
    }

    pub(crate) fn create(
        cx: &mut CallContext,
        context: AnyBaseAudioContext,
        options: &DynamicsCompressorOptions,
    ) -> WebIdlResult<Ref<Self>> {
        let initial_makeup_gain = match options.makeup_gain {
            FloatOrMakeupGainOptions::Float(value) => Some(value),
            FloatOrMakeupGainOptions::MakeupGainOptions(MakeupGainOptions::Automatic) => None,
        };
        let node = allocate(cx.vm(), |defer_gc| Self {
            node: AudioNode::new(defer_gc, context, 2),
            threshold: AudioParam::create(
                cx.vm(),
                context,
                None,
                -24.,
                Some(options.threshold),
                -100.,
                0.,
                AutomationRate::KRate,
                FixedAutomationRate::Yes,
                ParameterType::Simple,
            ),
            knee: AudioParam::create(
                cx.vm(),
                context,
                None,
                30.,
                Some(options.knee),
                0.,
                40.,
                AutomationRate::KRate,
                FixedAutomationRate::Yes,
                ParameterType::Simple,
            ),
            ratio: AudioParam::create(
                cx.vm(),
                context,
                None,
                12.,
                Some(options.ratio),
                1.,
                20.,
                AutomationRate::KRate,
                FixedAutomationRate::Yes,
                ParameterType::Simple,
            ),
            attack: AudioParam::create(
                cx.vm(),
                context,
                None,
                0.003,
                Some(options.attack),
                0.,
                1.,
                AutomationRate::KRate,
                FixedAutomationRate::Yes,
                ParameterType::Simple,
            ),
            release: AudioParam::create(
                cx.vm(),
                context,
                None,
                0.25,
                Some(options.release),
                0.,
                1.,
                AutomationRate::KRate,
                FixedAutomationRate::Yes,
                ParameterType::Simple,
            ),
            internal_reduction: Arc::new(AtomicU32::new(0_f32.to_bits())),
            makeup_gain: AudioParam::create(
                cx.vm(),
                context,
                None,
                0.,
                initial_makeup_gain,
                f32::MIN,
                770.6368,
                AutomationRate::KRate,
                FixedAutomationRate::Yes,
                ParameterType::Simple,
            ),
            automatic_makeup_gain: initial_makeup_gain.is_none(),
        });
        node.node.set_this(AnyAudioNode::DynamicsCompressorNode(node));
        node.node.event_target().set_owner(node);
        node.threshold.set_owner(AnyAudioNode::DynamicsCompressorNode(node));
        node.knee.set_owner(AnyAudioNode::DynamicsCompressorNode(node));
        node.ratio.set_owner(AnyAudioNode::DynamicsCompressorNode(node));
        node.attack.set_owner(AnyAudioNode::DynamicsCompressorNode(node));
        node.release.set_owner(AnyAudioNode::DynamicsCompressorNode(node));
        node.makeup_gain.set_owner(AnyAudioNode::DynamicsCompressorNode(node));

        // Default options for channel count and interpretation
        // https://webaudio.github.io/web-audio-api/#DynamicsCompressorNode
        let default_options = AudioNodeDefaultOptions {
            channel_count: 2,
            channel_count_mode: ChannelCountMode::ClampedMax,
            channel_interpretation: ChannelInterpretation::Speakers,
        };

        node.node
            .initialize_audio_node_options(cx, &options.parent, &default_options)?;

        if node.automatic_makeup_gain {
            node.makeup_gain
                .render_param()
                .publish_current_value(initial_automatic_makeup_gain(
                    options.threshold,
                    options.knee,
                    options.ratio,
                ));
        }
        let config = SetChannelConfig {
            node_id: node.node.node_id(),
            channel_count: node.node.channel_count_value() as usize,
            channel_count_mode: node.node.channel_count_mode(cx)?,
            channel_interpretation: node.node.channel_interpretation(cx)?,
        };
        let params = DynamicsCompressorParams {
            threshold: Arc::clone(node.threshold.render_param()),
            knee: Arc::clone(node.knee.render_param()),
            ratio: Arc::clone(node.ratio.render_param()),
            attack: Arc::clone(node.attack.render_param()),
            release: Arc::clone(node.release.render_param()),
            makeup_gain: Arc::clone(node.makeup_gain.render_param()),
        };
        node.node
            .queue_render_node_creation(create_dynamics_compressor_render_node(
                config,
                context.render_quantum_size_value() as usize,
                context.sample_rate_value(),
                params,
                node.automatic_makeup_gain,
                Arc::clone(&node.internal_reduction),
            ));
        Ok(node)
    }

    pub(crate) fn threshold(&self, _: &mut CallContext) -> WebIdlResult<Ref<AudioParam>> {
        Ok(self.threshold)
    }

    pub(crate) fn knee(&self, _: &mut CallContext) -> WebIdlResult<Ref<AudioParam>> {
        Ok(self.knee)
    }

    pub(crate) fn ratio(&self, _: &mut CallContext) -> WebIdlResult<Ref<AudioParam>> {
        Ok(self.ratio)
    }

    pub(crate) fn attack(&self, _: &mut CallContext) -> WebIdlResult<Ref<AudioParam>> {
        Ok(self.attack)
    }

    pub(crate) fn release(&self, _: &mut CallContext) -> WebIdlResult<Ref<AudioParam>> {
        Ok(self.release)
    }

    pub(crate) fn makeup_gain(&self, _: &mut CallContext) -> WebIdlResult<Ref<AudioParam>> {
        Ok(self.makeup_gain)
    }

    // https://webaudio.github.io/web-audio-api/#dom-dynamicscompressornode-reduction
    pub(crate) fn reduction(&self, _: &mut CallContext) -> WebIdlResult<f32> {
        Ok(f32::from_bits(self.internal_reduction.load(Ordering::Relaxed)))
    }

    // https://webaudio.github.io/web-audio-api/#dom-audionode-channelcount
    pub(crate) fn set_channel_count_override(&self, _: &mut CallContext, channel_count: u32) -> WebIdlResult<()> {
        if channel_count > 2 {
            return Err(Exception::Dom(
                "NotSupportedError",
                Utf16String::from_ascii(b"DynamicsCompressorNode does not support channel count greater than 2"),
            ));
        }

        self.node.set_channel_count_impl(channel_count)
    }

    // https://webaudio.github.io/web-audio-api/#dom-audionode-channelcountmode
    pub(crate) fn set_channel_count_mode_override(
        &self,
        _: &mut CallContext,
        mode: ChannelCountMode,
    ) -> WebIdlResult<()> {
        if mode == ChannelCountMode::Max {
            // Return a NotSupportedError if 'max' is used
            return Err(Exception::Dom(
                "NotSupportedError",
                Utf16String::from_ascii(b"DynamicsCompressorNode does not support 'max' as channelCountMode."),
            ));
        }

        // If the mode is valid, call the base class implementation
        self.node.set_channel_count_mode_impl(mode)
    }
}
