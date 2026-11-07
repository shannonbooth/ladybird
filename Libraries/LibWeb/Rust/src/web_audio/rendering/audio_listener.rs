/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::cell::{Ref, RefCell};
use std::sync::Arc;

use super::render_graph::RenderGraph;
use super::render_node::{RenderAudioParam, RenderContext};

pub struct RenderAudioListener {
    params: [Arc<RenderAudioParam>; 9],
    values: RefCell<[Vec<f32>; 9]>,
    pending_values: RefCell<[Vec<f32>; 9]>,
}

impl RenderAudioListener {
    pub fn new(params: [&Arc<RenderAudioParam>; 9], quantum_size: usize) -> Self {
        let defaults = [0., 0., 0., 0., 0., -1., 0., 1., 0.];
        Self {
            params: params.map(Arc::clone),
            values: RefCell::new(defaults.map(|value| vec![value; quantum_size])),
            pending_values: RefCell::new(std::array::from_fn(|_| vec![0.; quantum_size])),
        }
    }

    pub(super) fn compute_values(&self, graph: &RenderGraph, context: &RenderContext) {
        let mut pending_values = self.pending_values.borrow_mut();
        for (param, values) in self.params.iter().zip(pending_values.iter_mut()) {
            unsafe { param.compute(graph, context, values) };
        }
        // NB: Pulls through a panner during computation see the last complete listener values.
        std::mem::swap(&mut *pending_values, &mut *self.values.borrow_mut());
    }

    pub(super) fn values(&self) -> Ref<'_, [Vec<f32>; 9]> {
        self.values.borrow()
    }

    pub(super) fn params(&self) -> &[Arc<RenderAudioParam>; 9] {
        &self.params
    }
}
