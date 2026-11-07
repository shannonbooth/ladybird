/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use super::render_nodes::BusRef;

const KEMAR_SAMPLE_RATE: f64 = 44100.;
const RESPONSE_FRAMES: usize = 128;
const RESPONSE_BYTES: usize = RESPONSE_FRAMES * 2 * size_of::<i16>();
const AZIMUTH_COUNTS: [usize; 14] = [56, 60, 72, 72, 72, 72, 72, 60, 56, 45, 36, 24, 12, 1];
const ROW_OFFSETS: [usize; 14] = {
    let mut offsets = [0; 14];
    let mut row = 1;
    while row < offsets.len() {
        offsets[row] = offsets[row - 1] + (AZIMUTH_COUNTS[row - 1] / 2 + 1) * RESPONSE_BYTES;
        row += 1;
    }
    offsets
};
// KEMAR compact set by Bill Gardner and Keith Martin, MIT Media Lab: https://sound.media.mit.edu/resources/KEMAR.html
const RESPONSES: &[u8; ROW_OFFSETS[13] + RESPONSE_BYTES] = include_bytes!("kemar-compact.bin");

struct Response {
    offset: usize,
    swap_ears: bool,
}

impl Response {
    fn for_direction(row: usize, azimuth: usize) -> Self {
        let azimuth_count = AZIMUTH_COUNTS[row];
        let swap_ears = azimuth > azimuth_count / 2;
        let index = if swap_ears { azimuth_count - azimuth } else { azimuth };
        Self {
            offset: ROW_OFFSETS[row] + index * RESPONSE_BYTES,
            swap_ears,
        }
    }

    fn sample(&self, frame: usize, channel: usize) -> f32 {
        if frame >= RESPONSE_FRAMES {
            return 0.;
        }
        let ear = if self.swap_ears { 1 - channel } else { channel };
        let offset = self.offset + (frame * 2 + ear) * size_of::<i16>();
        f32::from(i16::from_le_bytes([RESPONSES[offset], RESPONSES[offset + 1]])) / 32768.
    }
}

struct AzimuthResponses {
    first: Response,
    second: Response,
    fraction: f32,
}

impl AzimuthResponses {
    fn new(row: usize, azimuth: f64) -> Self {
        let count = AZIMUTH_COUNTS[row];
        let index = azimuth.rem_euclid(360.) * count as f64 / 360.;
        let first = index as usize % count;
        Self {
            first: Response::for_direction(row, first),
            second: Response::for_direction(row, (first + 1) % count),
            fraction: index.fract() as f32,
        }
    }

    fn sample(&self, frame: usize, channel: usize) -> f32 {
        let first = self.first.sample(frame, channel);
        first + self.fraction * (self.second.sample(frame, channel) - first)
    }
}

pub(super) struct HrtfPanner {
    direction: Option<(f64, f64)>,
    impulse_response: Vec<[f32; 2]>,
    history: Vec<[f32; 2]>,
    history_index: usize,
    tail_frames: usize,
    resampling_step: f64,
}

impl HrtfPanner {
    pub(super) fn new(sample_rate: f32) -> Self {
        let response_frames = (RESPONSE_FRAMES as f64 * f64::from(sample_rate) / KEMAR_SAMPLE_RATE).ceil() as usize;
        Self {
            direction: None,
            impulse_response: vec![[0.; 2]; response_frames],
            history: vec![[0.; 2]; response_frames * 2],
            history_index: 0,
            tail_frames: 0,
            resampling_step: KEMAR_SAMPLE_RATE / f64::from(sample_rate),
        }
    }

    pub(super) fn reset(&mut self) {
        self.history.fill([0.; 2]);
        self.tail_frames = 0;
    }

    pub(super) fn has_tail(&self) -> bool {
        self.tail_frames != 0
    }

    fn update_impulse_response(&mut self, azimuth: f64, elevation: f64) {
        let direction = (azimuth, elevation);
        if self.direction == Some(direction) {
            return;
        }
        self.direction = Some(direction);
        let row = (elevation.clamp(-40., 90.) + 40.) / 10.;
        let first_row = row as usize;
        let second_row = (first_row + 1).min(AZIMUTH_COUNTS.len() - 1);
        let elevation_fraction = row.fract() as f32;
        let first = AzimuthResponses::new(first_row, azimuth);
        let second = AzimuthResponses::new(second_row, azimuth);

        for (frame, taps) in self.impulse_response.iter_mut().enumerate() {
            let source_frame = frame as f64 * self.resampling_step;
            let sample = source_frame as usize;
            let sample_fraction = source_frame.fract() as f32;
            for (ear, tap) in taps.iter_mut().enumerate() {
                let interpolate_elevation = |index| {
                    let sample = first.sample(index, ear);
                    sample + elevation_fraction * (second.sample(index, ear) - sample)
                };
                let a = interpolate_elevation(sample);
                let b = interpolate_elevation(sample + 1);
                *tap = (a + sample_fraction * (b - a)) * self.resampling_step as f32;
            }
        }
    }

    // https://webaudio.github.io/web-audio-api/#Spatialization-hrtf-panning
    pub(super) unsafe fn pan(&mut self, input: &BusRef, output: &BusRef, azimuth: f64, elevation: f64, gain: f32) {
        self.update_impulse_response(azimuth, elevation);
        let left = unsafe { input.channel(0) };
        let right = unsafe { input.channel(usize::from(input.channel_count() > 1)) };
        let output_left = unsafe { output.channel_mut(0) };
        let output_right = unsafe { output.channel_mut(1) };
        let response_frames = self.impulse_response.len();

        for frame in 0..left.len() {
            let input = [left[frame], right[frame]];
            let index = self.history_index;
            self.history[index] = input;
            self.history[index + response_frames] = input;
            self.history_index = (index + 1) % response_frames;

            let history = self.history[index + 1..=index + response_frames].iter().rev();
            let [mut left_sample, mut right_sample] = [0., 0.];
            if input != [0.; 2] || self.has_tail() {
                for (taps, samples) in self.impulse_response.iter().zip(history) {
                    left_sample += taps[0] * samples[0];
                    right_sample += taps[1] * samples[1];
                }
            }
            output_left[frame] = left_sample * gain;
            output_right[frame] = right_sample * gain;

            if input != [0.; 2] {
                self.tail_frames = response_frames - 1;
            } else {
                self.tail_frames = self.tail_frames.saturating_sub(1);
            }
        }
    }
}
