/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::sync::Arc;
use std::sync::atomic::{AtomicU32, Ordering};

use ak::math;

use super::render_graph::RenderGraph;
use super::render_node::{ChannelInterpretation, NodeHandle, RenderAudioParam, RenderContext, RenderNodeData};
use super::render_nodes::{BusRef, NodeProcessor, node_handle};
use crate::web_audio::control_message::SetChannelConfig;

pub struct DynamicsCompressorParams {
    pub threshold: Arc<RenderAudioParam>,
    pub knee: Arc<RenderAudioParam>,
    pub ratio: Arc<RenderAudioParam>,
    pub attack: Arc<RenderAudioParam>,
    pub release: Arc<RenderAudioParam>,
    pub makeup_gain: Arc<RenderAudioParam>,
}

fn decibels_to_linear(value: f64) -> f64 {
    math::pow(10., value / 20.)
}

// https://webaudio.github.io/web-audio-api/#linear-to-decibel
pub fn linear_to_decibels(value: f64) -> f64 {
    // 1. If \(v\) is equal to zero, return -1000.
    if value == 0. {
        return -1000.;
    }
    // 2. Else, return \( 20 \, \log_{10}{v} \).
    20. * math::log10(value)
}

struct CompressionCurve {
    threshold_db: f64,
    threshold: f64,
    knee_db: f64,
    knee_end_threshold: f64,
    slope: f64,
}

impl CompressionCurve {
    fn new(threshold_db: f64, knee_db: f64, ratio: f64) -> Self {
        Self {
            threshold_db,
            threshold: decibels_to_linear(threshold_db),
            knee_db,
            knee_end_threshold: decibels_to_linear(threshold_db + knee_db),
            slope: 1. / ratio,
        }
    }

    // https://webaudio.github.io/web-audio-api/#compression-curve
    fn apply(&self, value: f64) -> f64 {
        // 5. This function is the identity up to the value of the linear threshold (i.e., \(f(x) = x\)).
        if value <= self.threshold {
            return value;
        }
        let level = linear_to_decibels(value);
        let above_threshold = level - self.threshold_db;

        // 6. From the threshold up to the knee end threshold, User-Agents can choose the curve shape. The whole function MUST be monotonically increasing and continuous.
        // Note: If the knee is 0, the DynamicsCompressorNode is called a hard-knee compressor.
        if value < self.knee_end_threshold {
            let output_level = level + (self.slope - 1.) * above_threshold * above_threshold / (2. * self.knee_db);
            return decibels_to_linear(output_level);
        }

        // 7. This function is linear, based on the ratio, after the threshold and the soft knee (i.e., \(f(x) = \frac{1}{ratio} \cdot x \)).
        let knee_offset = self.knee_db * (1. - self.slope) / 2.;
        decibels_to_linear(self.threshold_db + knee_offset + self.slope * above_threshold)
    }

    // https://webaudio.github.io/web-audio-api/#computing-the-makeup-gain
    fn compute_makeup_gain(&self) -> f64 {
        // 1. Let full range gain be the value returned by applying the compression curve to the value 1.0.
        let full_range_gain = self.apply(1.);
        // 2. Let full range makeup gain be the inverse of full range gain.
        let full_range_makeup_gain = 1. / full_range_gain;
        // 3. Return the result of taking the 0.6 power of full range makeup gain.
        math::pow(full_range_makeup_gain, 0.6)
    }
}

pub fn initial_automatic_makeup_gain(threshold: f32, knee: f32, ratio: f32) -> f32 {
    let curve = CompressionCurve::new(
        f64::from(threshold.clamp(-100., 0.)),
        f64::from(knee.clamp(0., 40.)),
        f64::from(ratio.clamp(1., 20.)),
    );
    linear_to_decibels(curve.compute_makeup_gain()) as f32
}

// https://webaudio.github.io/web-audio-api/#envelopefollower
struct EnvelopeFollower {
    detector_average: f64,
    compressor_gain: f64,
}

impl EnvelopeFollower {
    // https://webaudio.github.io/web-audio-api/#computing-the-envelope-rate
    fn compute_envelope_rate(releasing: bool, attack: f64, release: f64, ratio: f64) -> f64 {
        if releasing {
            1. + (1. + 1. / (1. + ratio)) / release.max(1.)
        } else if attack == 0. {
            1.
        } else {
            1. - math::exp(-ratio / attack)
        }
    }

    // https://webaudio.github.io/web-audio-api/#DynamicsCompressorOptions-processing
    #[allow(clippy::too_many_arguments)]
    unsafe fn reduction_gain(
        &mut self,
        input: &BusRef,
        params: &DynamicsCompressorParams,
        automatic_makeup_gain: bool,
        internal_reduction: &AtomicU32,
        graph: &RenderGraph,
        context: &RenderContext,
        gain: &mut [f32],
    ) {
        let mut value = [0.];
        let mut compute = |param: &RenderAudioParam| {
            unsafe { param.compute(graph, context, None, &mut value) };
            f64::from(value[0])
        };

        // 1. Let attack and release have the values of attack and release, respectively, sampled at the time of processing (those are k-rate parameters), mutiplied by the sample-rate of the BaseAudioContext this DynamicsCompressorNode is associated with.
        let attack = compute(&params.attack) * context.sample_rate;
        let release = compute(&params.release) * context.sample_rate;

        // 1. Let threshold and knee have the values of threshold and knee, respectively, converted to linear units and sampled at the time of processing of this block (as k-rate parameters).
        // 2. Calculate the sum of threshold plus knee also sampled at the time of processing of this block (as k-rate parameters).
        // 3. Let knee end threshold have the value of this sum converted to linear units.
        // 4. Let ratio have the value of the ratio, sampled at the time of processing of this block (as a k-rate parameter).
        let compression_curve = CompressionCurve::new(
            compute(&params.threshold),
            compute(&params.knee),
            compute(&params.ratio),
        );

        // 2. Let makeup gain be the return value of computing the makeup gain if [[automatic makeup gain]] is true. Otherwise, let makeup gain have the value of makeupGain, converted to linear units and sampled at the time of processing (as a k-rate parameter).
        let makeup_gain = if automatic_makeup_gain {
            compression_curve.compute_makeup_gain()
        } else {
            decibels_to_linear(compute(&params.makeup_gain))
        };

        // 3. Let detector average be the value of the slot [[detector average]].
        let mut detector_average = self.detector_average;

        // 4. Let compressor gain be the value of the slot [[compressor gain]].
        let mut compressor_gain = self.compressor_gain;

        let channel_count = unsafe { input.channel_count() }.min(2);
        let mut metering_gain = 0.;
        let mut has_signal = false;

        // 5. For each sample input of the render quantum to be processed, execute the following steps:
        for (frame, gain) in gain.iter_mut().enumerate() {
            let input = (0..channel_count)
                .map(|channel| f64::from(unsafe { input.channel(channel) }[frame]).abs())
                .fold(0., f64::max);
            has_signal |= input != 0.;

            // 5.1. If the absolute value of input is less than 0.0001, let attenuation be 1.0. Else, let shaped input be the value of applying the compression curve to the absolute value of input. Let attenuation be shaped input divided by the absolute value of input.
            let attenuation = if input < 0.0001 {
                1.
            } else {
                compression_curve.apply(input) / input
            };

            // 5.2. Let releasing be true if attenuation is greater than compressor gain, false otherwise.
            let releasing = attenuation > compressor_gain;

            // 5.3. Let detector rate be the result of applying the detector curve to attenuation.
            let detector_rate = 0.99 + 0.01 * attenuation;

            // 5.4. Subtract detector average from attenuation, and multiply the result by detector rate. Add this new result to detector average.
            detector_average += (attenuation - detector_average) * detector_rate;

            // 5.5. Clamp detector average to a maximum of 1.0.
            detector_average = detector_average.min(1.);

            // 5.6. Let envelope rate be the result of computing the envelope rate based on values of attack and release.
            let envelope_rate = Self::compute_envelope_rate(
                releasing,
                attack,
                release,
                compressor_gain / detector_average.max(f64::MIN_POSITIVE),
            );

            // 5.7. If releasing is true, set compressor gain to be the product of compressor gain and envelope rate, clamped to a maximum of 1.0.
            if releasing {
                compressor_gain = (compressor_gain * envelope_rate).min(1.);
            } else {
                // 5.8. Else, if releasing is false, let gain increment to be detector average minus compressor gain. Multiply gain increment by envelope rate, and add the result to compressor gain.
                let gain_increment = (detector_average - compressor_gain) * envelope_rate;
                compressor_gain += gain_increment;
            }

            // 5.9. Compute reduction gain to be compressor gain multiplied by makeup gain.
            let reduction_gain = compressor_gain * makeup_gain;
            *gain = reduction_gain as f32;

            // 5.10. Compute metering gain to be reduction gain, converted to decibel.
            metering_gain = linear_to_decibels(reduction_gain);
        }

        // 6. Set [[compressor gain]] to compressor gain.
        self.compressor_gain = compressor_gain;

        // 7. Set [[detector average]] to detector average.
        self.detector_average = detector_average;

        // 8. Atomically set the internal slot [[internal reduction]] to the value of metering gain.
        // Note: This step makes the metering gain update once per block, at the end of the block processing.
        // AD-HOC: The reduction attribute requires zero when the input is silent.
        let reduction = if has_signal { metering_gain as f32 } else { 0. };
        internal_reduction.store(reduction.to_bits(), Ordering::Relaxed);

        // 9. If [[automatic makeup gain]] is true, atomically set the internal slot [[current value]] of makeupGain to the value of makeup gain, converted to decibel.
        if automatic_makeup_gain {
            params
                .makeup_gain
                .publish_current_value(linear_to_decibels(makeup_gain) as f32);
        }
    }
}

struct DynamicsCompressorRenderNode {
    params: DynamicsCompressorParams,
    automatic_makeup_gain: bool,
    internal_reduction: Arc<AtomicU32>,
    envelope_follower: EnvelopeFollower,
    reduction_gain: Vec<f32>,
    predelay: Vec<[f32; 2]>,
    delay_frames: f64,
    delay_index: usize,
    stereo_tail_frames: usize,
}

impl NodeProcessor for DynamicsCompressorRenderNode {
    fn for_each_param(&self, callback: &mut dyn FnMut(&RenderAudioParam)) {
        for param in [
            &self.params.threshold,
            &self.params.knee,
            &self.params.ratio,
            &self.params.attack,
            &self.params.release,
            &self.params.makeup_gain,
        ] {
            callback(param);
        }
    }

    // https://webaudio.github.io/web-audio-api/#DynamicsCompressorOptions-processing
    unsafe fn process(&mut self, data: &RenderNodeData, graph: &RenderGraph, context: &RenderContext) {
        unsafe {
            let input = BusRef(data.pull_input(graph, context, 0).cast_mut());
            let output = BusRef(data.output(0));
            self.envelope_follower.reduction_gain(
                &input,
                &self.params,
                self.automatic_makeup_gain,
                &self.internal_reduction,
                graph,
                context,
                &mut self.reduction_gain,
            );

            let input_channels = input.channel_count().min(2);
            let output_channels = if input_channels == 2 || self.stereo_tail_frames != 0 {
                2
            } else {
                1
            };
            if input_channels == 2 {
                self.stereo_tail_frames = self.delay_frames.ceil() as usize;
            } else {
                self.stereo_tail_frames = self.stereo_tail_frames.saturating_sub(context.quantum_size);
            }
            output.set_channel_count(output_channels);

            let speakers = data.channel_interpretation.get() == ChannelInterpretation::Speakers;
            let delay_length = self.predelay.len();
            for frame in 0..context.quantum_size {
                let left = input.channel(0)[frame];
                let right = if input_channels == 2 {
                    input.channel(1)[frame]
                } else if speakers {
                    left
                } else {
                    0.
                };
                self.predelay[self.delay_index] = [left, right];
                let read_position = (self.delay_index as f64 - self.delay_frames).rem_euclid(delay_length as f64);
                let first_index = read_position as usize % delay_length;
                let second_index = (first_index + 1) % delay_length;
                let fraction = read_position.fract() as f32;
                for channel in 0..output_channels {
                    let a = self.predelay[first_index][channel];
                    let b = self.predelay[second_index][channel];
                    output.channel_mut(channel)[frame] = (a + fraction * (b - a)) * self.reduction_gain[frame];
                }
                self.delay_index = (self.delay_index + 1) % delay_length;
            }
        }
    }
}

pub fn create_dynamics_compressor_render_node(
    config: SetChannelConfig,
    quantum_size: usize,
    sample_rate: f32,
    params: DynamicsCompressorParams,
    automatic_makeup_gain: bool,
    internal_reduction: Arc<AtomicU32>,
) -> Box<NodeHandle> {
    let mut data = RenderNodeData::new(config.node_id, 1, 1, quantum_size, 1);
    data.reserve_output_channels(0, 2);
    data.channel_count.set(config.channel_count);
    data.channel_count_mode.set(config.channel_count_mode);
    data.channel_interpretation.set(config.channel_interpretation);
    let delay_frames = 0.006 * f64::from(sample_rate);
    node_handle(
        data,
        DynamicsCompressorRenderNode {
            params,
            automatic_makeup_gain,
            internal_reduction,
            envelope_follower: EnvelopeFollower {
                detector_average: 0.,
                compressor_gain: 1.,
            },
            reduction_gain: vec![0.; quantum_size],
            predelay: vec![[0.; 2]; delay_frames.ceil() as usize + 2],
            delay_frames,
            delay_index: 0,
            stereo_tail_frames: 0,
        },
    )
}
