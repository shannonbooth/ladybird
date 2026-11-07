/*
 * Copyright (c) 2018-2025, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2023, Jelle Raaijmakers <jelle@ladybird.org>
 * Copyright (c) 2026, Jelle Raaijmakers <jelle@ladybird.org>
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::cell::{Cell, RefCell};
use std::collections::HashMap;

use super::audio_bus::AudioBus;
use super::audio_listener::RenderAudioListener;
use super::node_message::node_message_target;
use super::render_node::{NodeHandle, RenderContext};
use crate::graph::nodes_on_a_cycle;
use crate::web_audio::control_message::ControlMessage;

// The rendering thread's mirror of the audio graph. Nodes and connections are added and updated exclusively through
// control messages, which are applied at the start of each render quantum.
// https://webaudio.github.io/web-audio-api/#rendering-loop
pub struct RenderGraph {
    nodes: NodeMap,
    destination_id: u64,
    listener: Option<Box<RenderAudioListener>>,
    ordered_node_list: RefCell<Vec<u64>>,
    current_quantum: Cell<u64>,
    incoming_connections_dirty: Cell<bool>,
}

impl Default for RenderGraph {
    fn default() -> Self {
        Self {
            nodes: NodeMap::default(),
            destination_id: 0,
            listener: None,
            ordered_node_list: RefCell::new(Vec::new()),
            current_quantum: Cell::new(0),
            incoming_connections_dirty: Cell::new(false),
        }
    }
}

impl RenderGraph {
    pub fn apply_control_messages(&mut self, messages: Vec<ControlMessage>) {
        for message in messages {
            match message {
                ControlMessage::AddNode(add_node) => {
                    let node_id = add_node.node.data().node_id;
                    self.nodes.insert(node_id, add_node.node);
                    self.incoming_connections_dirty.set(true);
                }
                ControlMessage::RemoveNode(remove_node) => {
                    if self.nodes.remove(&remove_node.node_id).is_some() {
                        self.incoming_connections_dirty.set(true);
                    }
                }
                ControlMessage::ReplaceConnections(replace_connections) => {
                    if let Some(source_node) = self.node(replace_connections.source) {
                        unsafe {
                            source_node.data().set_outgoing_connections(
                                replace_connections.node_connections,
                                replace_connections.param_connections,
                            );
                        }
                        self.incoming_connections_dirty.set(true);
                    }
                }
                ControlMessage::SetChannelConfig(config) => {
                    if let Some(target_node) = self.node(config.node_id) {
                        target_node.data().channel_count.set(config.channel_count);
                        target_node.data().channel_count_mode.set(config.channel_count_mode);
                        target_node
                            .data()
                            .channel_interpretation
                            .set(config.channel_interpretation);
                    }
                }
                ControlMessage::SetAudioListener(listener) => {
                    self.listener = Some(listener);
                    self.incoming_connections_dirty.set(true);
                }
                ControlMessage::NodeMessage(node_message) => {
                    if let Some(target_node) = self.node(node_message_target(&node_message)) {
                        unsafe {
                            target_node.node.handle_message(&node_message);
                        }
                    }
                }
            }
        }
    }

    // Renders a single quantum by pulling the destination node and any active source nodes.
    pub fn render_quantum(&self, context: &RenderContext) {
        self.current_quantum.set(self.current_quantum.get().wrapping_add(1));
        if self.incoming_connections_dirty.get() {
            unsafe {
                self.rebuild_incoming_connections();
            }
            self.order_audio_nodes();
            self.incoming_connections_dirty.set(false);
        }

        // 4.3. Compute the value(s) of the AudioListener’s AudioParams for this block.
        if let Some(listener) = self.listener() {
            listener.compute_values(self, context);
        }

        // 4.4. For each AudioNode, in ordered node list:
        let ordered_node_list = self.ordered_node_list.borrow();
        for node_id in &*ordered_node_list {
            self.process_node(self.node(*node_id).unwrap(), context);
        }

        // NB: A DelayReader only observes samples from earlier quanta, so writes can follow all reads.
        for node in self.nodes.values() {
            if node.data().is_cycle_breaker.get() {
                unsafe { node.node.write_delay(self, context) };
            }
        }
    }

    // Returns the source node's output bus, processing the node first if it has not been processed in this quantum.
    pub fn pull_output(&self, source: u64, output_index: usize, context: &RenderContext) -> *mut AudioBus {
        let Some(source_node) = self.node(source) else {
            return std::ptr::null_mut();
        };
        let data = source_node.data();
        if output_index >= data.output_count() || data.is_processing.get() {
            return std::ptr::null_mut();
        }
        self.process_node(source_node, context);
        unsafe { data.output(output_index) }
    }

    fn process_node(&self, node: &NodeHandle, context: &RenderContext) {
        let data = node.data();
        if data.processed_quantum.get() == self.current_quantum.get() || data.is_processing.get() {
            return;
        }
        if data.is_muted.get() {
            for index in 0..data.output_count() {
                unsafe { &mut *data.output(index) }.zero();
            }
        } else {
            data.is_processing.set(true);
            unsafe { node.node.process(self, context) };
            data.is_processing.set(false);
        }
        data.processed_quantum.set(self.current_quantum.get());
    }

    pub fn node(&self, node_id: u64) -> Option<&NodeHandle> {
        self.nodes.get(&node_id)
    }

    pub fn set_destination(&mut self, node_id: u64) {
        self.destination_id = node_id;
    }

    pub(super) fn listener(&self) -> Option<&RenderAudioListener> {
        self.listener.as_deref()
    }

    pub fn destination(&self) -> Option<&NodeHandle> {
        self.node(self.destination_id)
    }

    // Retire ended sources even if a cycle muted their processing for this quantum.
    pub fn take_ended_nodes(&mut self, context: &RenderContext) -> Vec<u64> {
        let mut ended_nodes = Vec::new();
        for (&node_id, graph_node) in self.nodes.iter() {
            if unsafe { graph_node.node.has_ended(context) } {
                ended_nodes.push(node_id);
            }
        }
        for node_id in &ended_nodes {
            self.nodes.remove(node_id);
        }
        if !ended_nodes.is_empty() {
            self.incoming_connections_dirty.set(true);
        }
        ended_nodes
    }

    // https://webaudio.github.io/web-audio-api/#rendering-a-graph
    fn order_audio_nodes(&self) {
        // 4.2.1. Let ordered node list be an empty list of AudioNodes and AudioListener. It will contain an ordered list of AudioNodes and the AudioListener when this ordering algorithm terminates.
        let mut ordered_node_list = self.ordered_node_list.borrow_mut();
        ordered_node_list.clear();

        // 4.2.2. Let nodes be the set of all nodes created by this BaseAudioContext, and still alive.
        let nodes: Vec<_> = self.nodes.values().collect();
        let node_indices: HashMap<_, _> = nodes
            .iter()
            .enumerate()
            .map(|(index, node)| (node.data().node_id, index))
            .collect();

        // 4.2.3. Add the AudioListener to nodes.
        // NB: The final index represents the AudioListener.
        let mut edges = vec![Vec::new(); nodes.len() + 1];
        let mut audio_input_counts = Vec::with_capacity(nodes.len());
        for (index, node) in nodes.iter().enumerate() {
            let inputs = unsafe { &*node.data().input_connections.get() };
            edges[index].extend(
                inputs
                    .iter()
                    .flatten()
                    .filter_map(|input| node_indices.get(&input.source).copied()),
            );
            audio_input_counts.push(edges[index].len());
            node.node.for_each_param(&mut |param| unsafe {
                param.for_each_input(&mut |source| edges[index].extend(node_indices.get(&source).copied()));
            });
        }
        if let Some(listener) = self.listener() {
            for param in listener.params() {
                unsafe {
                    param.for_each_input(&mut |source| edges[nodes.len()].extend(node_indices.get(&source).copied()));
                }
            }
        }

        // 4.2.4. Let cycle breakers be an empty set of DelayNodes. It will contain all the DelayNodes that are part of a cycle.
        let mut cycle_breakers = Vec::new();

        let on_a_cycle = nodes_on_a_cycle(&edges);
        // 4.2.5. For each AudioNode node in nodes:
        for (index, node) in nodes.iter().enumerate() {
            // 4.2.5.1. If node is a DelayNode that is part of a cycle, add it to cycle breakers and remove it from nodes.
            node.data().is_cycle_breaker.set(false);
            if node.data().is_delay && on_a_cycle[index] {
                cycle_breakers.push(index);
            }
        }

        let split_delays = !cycle_breakers.is_empty();

        // 4.2.6. For each DelayNode delay in cycle breakers:
        for delay in cycle_breakers {
            // 4.2.6.1. Let delayWriter and delayReader respectively be a DelayWriter and a DelayReader, for delay. Add delayWriter and delayReader to nodes. Disconnect delay from all its input and outputs.
            // Note: This breaks the cycle: if a DelayNode is in a cycle, its two ends can be considered separately, because delay lines cannot be smaller than one render quantum when in a cycle.
            // AD-HOC: The reader keeps its delayTime inputs, so a cycle through delayTime is still muted.
            // NB: The writer runs after the ordered list.
            nodes[delay].data().is_cycle_breaker.set(true);
            edges[delay].drain(..audio_input_counts[delay]);
        }

        // 4.2.7. If nodes contains cycles, mute all the AudioNodes that are part of this cycle, and remove them from nodes.
        let mut marked = if split_delays {
            nodes_on_a_cycle(&edges)
        } else {
            on_a_cycle
        };
        for (node, &muted) in nodes.iter().zip(&marked) {
            node.data().is_muted.set(muted);
        }

        // 4.2.8. Consider all elements in nodes to be unmarked. While there are unmarked elements in nodes:
        let mut walk = Vec::new();
        for node in 0..edges.len() {
            // 4.2.8.1. Choose an element node in nodes.
            // 4.2.8.2. Visit node.
            if !marked[node] {
                Self::visit(node, &nodes, &edges, &mut marked, &mut walk, &mut ordered_node_list);
            }
        }

        // 4.2.9. Reverse the order of ordered node list.
        // NB: Visit appends instead of prepending, so the order is already reversed.
    }

    // https://webaudio.github.io/web-audio-api/#visit
    fn visit(
        node: usize,
        nodes: &[&NodeHandle],
        edges: &[Vec<usize>],
        marked: &mut [bool],
        walk: &mut Vec<(usize, usize)>,
        ordered_node_list: &mut Vec<u64>,
    ) {
        // 1. If node is marked, abort these steps.
        // 2. Mark node.
        marked[node] = true;
        walk.push((node, 0));
        while let Some((node, next_edge)) = walk.last_mut() {
            let node = *node;
            // 3. If node is an AudioNode, Visit each AudioNode connected to the input of node.
            // 4. For each AudioParam param of node:
            // 4.1. For each AudioNode param input node connected to param:
            // 4.1.1. Visit param input node
            if let Some(&dependency) = edges[node].get(*next_edge) {
                *next_edge += 1;
                if !marked[dependency] {
                    marked[dependency] = true;
                    walk.push((dependency, 0));
                }
            } else {
                // 5. Add node to the beginning of ordered node list.
                // NB: The listener is computed separately in step 4.3.
                if let Some(node) = nodes.get(node) {
                    ordered_node_list.push(node.data().node_id);
                }
                walk.pop();
            }
        }
    }

    unsafe fn rebuild_incoming_connections(&self) {
        if let Some(listener) = self.listener() {
            for param in listener.params() {
                unsafe { param.clear_inputs() };
            }
        }
        for graph_node in self.nodes.values() {
            let data = graph_node.data();
            for input_index in 0..data.input_count() {
                (unsafe { &mut *data.input_connections.get() })[input_index].clear();
            }
            graph_node.node.for_each_param(&mut |param| unsafe {
                param.clear_inputs();
            });
        }
        for (&node_id, graph_node) in self.nodes.iter() {
            let data = graph_node.data();
            for connection in unsafe { &*data.outgoing_node_connections.get() } {
                if let Some(destination_node) = self.node(connection.destination)
                    && connection.input_index < destination_node.data().input_count()
                {
                    (unsafe { &mut *destination_node.data().input_connections.get() })[connection.input_index].push(
                        super::render_node::Connection {
                            source: node_id,
                            output_index: connection.output_index,
                        },
                    );
                }
            }
            for connection in unsafe { &*data.outgoing_param_connections.get() } {
                unsafe {
                    connection.destination.add_input(node_id, connection.output_index);
                }
            }
        }
    }
}

#[derive(Default)]
struct NodeMap {
    buckets: Vec<Option<(u64, Box<NodeHandle>)>>,
    size: usize,
}

impl NodeMap {
    fn hash(mut key: u64) -> usize {
        key ^= key >> 33;
        key = key.wrapping_mul(0xff51afd7ed558ccd);
        key ^= key >> 33;
        key = key.wrapping_mul(0xc4ceb9fe1a85ec53);
        key ^= key >> 33;
        key as u32 as usize
    }

    fn probe_length(&self, key: u64, index: usize) -> usize {
        index.wrapping_sub(Self::hash(key)) & (self.buckets.len() - 1)
    }

    fn insert(&mut self, key: u64, value: Box<NodeHandle>) {
        if (self.size + 1) * 100 >= self.buckets.len() * 70 {
            let capacity = (self.buckets.len() * 2).max(8);
            let old_buckets = std::mem::replace(&mut self.buckets, (0..capacity).map(|_| None).collect());
            self.size = 0;
            for (key, value) in old_buckets.into_iter().flatten() {
                self.write_value(key, value);
            }
        }
        self.write_value(key, value);
    }

    fn write_value(&mut self, mut key: u64, mut value: Box<NodeHandle>) {
        let mask = self.buckets.len() - 1;
        let mut index = Self::hash(key) & mask;
        let mut probe_length = 0;
        loop {
            let Some((target_key, _)) = &self.buckets[index] else {
                self.buckets[index] = Some((key, value));
                self.size += 1;
                return;
            };
            if *target_key == key {
                self.buckets[index] = Some((key, value));
                return;
            }
            let target_probe_length = self.probe_length(*target_key, index);
            if probe_length > target_probe_length {
                let displaced = self.buckets[index].replace((key, value)).unwrap();
                (key, value) = displaced;
                probe_length = target_probe_length;
            }
            index = (index + 1) & mask;
            probe_length += 1;
        }
    }

    fn index(&self, key: u64) -> Option<usize> {
        if self.size == 0 {
            return None;
        }
        let mask = self.buckets.len() - 1;
        let mut index = Self::hash(key) & mask;
        let mut probe_length = 0;
        loop {
            let (target_key, _) = self.buckets[index].as_ref()?;
            if *target_key == key {
                return Some(index);
            }
            if probe_length > self.probe_length(*target_key, index) {
                return None;
            }
            index = (index + 1) & mask;
            probe_length += 1;
        }
    }

    fn get(&self, key: &u64) -> Option<&NodeHandle> {
        self.index(*key)
            .map(|index| self.buckets[index].as_ref().unwrap().1.as_ref())
    }

    fn remove(&mut self, key: &u64) -> Option<Box<NodeHandle>> {
        let mut shift_to_index = self.index(*key)?;
        let (_, value) = self.buckets[shift_to_index].take().unwrap();
        self.size -= 1;
        let mask = self.buckets.len() - 1;
        loop {
            let shift_from_index = (shift_to_index + 1) & mask;
            let Some((next_key, _)) = self.buckets[shift_from_index].as_ref() else {
                break;
            };
            if self.probe_length(*next_key, shift_from_index) == 0 {
                break;
            }
            self.buckets[shift_to_index] = self.buckets[shift_from_index].take();
            shift_to_index = shift_from_index;
        }
        Some(value)
    }

    fn iter(&self) -> impl Iterator<Item = (&u64, &NodeHandle)> {
        self.buckets
            .iter()
            .filter_map(Option::as_ref)
            .map(|(key, value)| (key, value.as_ref()))
    }

    fn values(&self) -> impl Iterator<Item = &NodeHandle> {
        self.iter().map(|(_, value)| value)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::css::style::pipeline::allocation_count::count_allocations;
    use crate::web_audio::audio_param_timeline::AudioParamTimeline;
    use crate::web_audio::control_message::*;
    use crate::web_audio::control_message_queue::ControlMessageQueue;
    use crate::web_audio::rendering::node_message::{NodeMessage, StartSource, StopSource};
    use crate::web_audio::rendering::render_node::{
        AutomationRate, ChannelCountMode, ChannelInterpretation, OutgoingNodeConnection, OutgoingParamConnection,
        ParameterType, RenderAudioParam,
    };
    use crate::web_audio::rendering::render_nodes::{
        create_channel_merger_render_node, create_constant_source_render_node, create_destination_render_node,
        create_gain_render_node,
    };
    use std::sync::Arc;

    #[test]
    fn control_queue_rendering_and_ended_node_retirement() {
        let timeline = Arc::new(AudioParamTimeline::new(0.75));
        let param = Arc::new(RenderAudioParam::new(
            timeline,
            -1.,
            1.,
            ParameterType::Simple,
            AutomationRate::ARate,
            4,
        ));
        let queue = ControlMessageQueue::default();
        queue.enqueue(ControlMessage::SetChannelConfig(SetChannelConfig {
            node_id: 1,
            channel_count: 42,
            channel_count_mode: ChannelCountMode::Explicit,
            channel_interpretation: ChannelInterpretation::Discrete,
        }));
        for id in [1, 3] {
            let node = create_constant_source_render_node(id, 4, Arc::clone(&param));
            queue.enqueue(ControlMessage::AddNode(AddNode { node }));
            queue.enqueue(ControlMessage::NodeMessage(NodeMessage::StartSource(StartSource {
                node_id: id,
                when: 1. / 48000.,
            })));
            queue.enqueue(ControlMessage::NodeMessage(NodeMessage::StopSource(StopSource {
                node_id: id,
                when: 3. / 48000.,
            })));
        }
        let destination = create_destination_render_node(2, 4, 2);
        queue.enqueue(ControlMessage::AddNode(AddNode { node: destination }));
        queue.enqueue(ControlMessage::ReplaceConnections(ReplaceConnections {
            source: 1,
            node_connections: vec![OutgoingNodeConnection {
                destination: 2,
                output_index: 0,
                input_index: 0,
            }],
            param_connections: Vec::new(),
        }));
        let mut graph = RenderGraph::default();
        graph.set_destination(2);
        graph.apply_control_messages(queue.drain());
        assert!(queue.drain().is_empty());
        assert_eq!(graph.node(1).unwrap().data().channel_count.get(), 2);
        assert_eq!(Arc::strong_count(&param), 3);
        let mut context = RenderContext {
            sample_rate: 48000.,
            quantum_size: 4,
            quantum_start_frame: 0,
        };
        graph.render_quantum(&context);
        let bus = unsafe { &*graph.destination().unwrap().data().output(0) };
        assert_eq!(bus.channel(0), &[0., 0.75, 0.75, 0.]);
        assert_eq!(bus.channel(1), &[0., 0.75, 0.75, 0.]);
        let mut ended = graph.take_ended_nodes(&context);
        ended.sort();
        assert_eq!(ended, [1, 3]);
        assert_eq!(Arc::strong_count(&param), 1);
        context.quantum_start_frame = 4;
        graph.render_quantum(&context);
        assert!(unsafe { &*graph.destination().unwrap().data().output(0) }.is_silent());
        assert!(graph.take_ended_nodes(&context).is_empty());
    }

    #[test]
    fn connected_params_and_bus_layout_changes_do_not_allocate_during_processing() {
        let context = RenderContext {
            sample_rate: 48000.,
            quantum_size: 4,
            quantum_start_frame: 0,
        };
        let source_param = Arc::new(RenderAudioParam::new(
            Arc::new(AudioParamTimeline::new(0.5)),
            -1.,
            1.,
            ParameterType::Simple,
            AutomationRate::ARate,
            context.quantum_size,
        ));
        let gain_param = Arc::new(RenderAudioParam::new(
            Arc::new(AudioParamTimeline::new(1.)),
            f32::MIN,
            f32::MAX,
            ParameterType::Simple,
            AutomationRate::ARate,
            context.quantum_size,
        ));
        let mut graph = RenderGraph::default();
        graph.set_destination(3);
        graph.apply_control_messages(vec![
            ControlMessage::AddNode(AddNode {
                node: create_constant_source_render_node(1, context.quantum_size, source_param),
            }),
            ControlMessage::AddNode(AddNode {
                node: create_channel_merger_render_node(2, context.quantum_size, 6),
            }),
            ControlMessage::AddNode(AddNode {
                node: create_gain_render_node(3, context.quantum_size, Arc::clone(&gain_param)),
            }),
            ControlMessage::NodeMessage(NodeMessage::StartSource(StartSource { node_id: 1, when: 0. })),
        ]);
        for channel_count in [1, 6, 1, 6] {
            let input = if channel_count == 1 { 3 } else { 2 };
            graph.apply_control_messages(vec![
                ControlMessage::ReplaceConnections(ReplaceConnections {
                    source: 1,
                    node_connections: vec![OutgoingNodeConnection {
                        destination: input,
                        output_index: 0,
                        input_index: 0,
                    }],
                    param_connections: vec![OutgoingParamConnection {
                        destination: Arc::clone(&gain_param),
                        output_index: 0,
                    }],
                }),
                ControlMessage::ReplaceConnections(ReplaceConnections {
                    source: 2,
                    node_connections: if channel_count == 1 {
                        Vec::new()
                    } else {
                        vec![OutgoingNodeConnection {
                            destination: 3,
                            output_index: 0,
                            input_index: 0,
                        }]
                    },
                    param_connections: Vec::new(),
                }),
            ]);
            // Apply topology changes before counting DSP allocations.
            unsafe { graph.rebuild_incoming_connections() };
            graph.incoming_connections_dirty.set(false);
            let (_, allocations) = count_allocations(|| graph.render_quantum(&context));
            assert_eq!(allocations.everywhere, 0);
            let output = unsafe { &*graph.destination().unwrap().data().output(0) };
            assert_eq!(output.channel_count(), channel_count);
            assert_eq!(output.channel(0), &[0.75; 4]);
        }
    }
}
