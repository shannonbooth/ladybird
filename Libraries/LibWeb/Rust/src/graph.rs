/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/// Which nodes of the graph `edges` describes lie on a cycle of it, one with an edge to itself among them. Those are the
/// nodes of a strongly connected component of more than one node, which Tarjan's algorithm finds in time linear in the
/// nodes and edges. It walks with a stack of its own, as a chain may be as long as a page likes.
pub(crate) fn nodes_on_a_cycle(edges: &[Vec<usize>]) -> Vec<bool> {
    // The order each node was first seen in, from 1, and the earliest such order on the walk's stack that it reaches.
    let mut order = vec![0usize; edges.len()];
    let mut lowest = vec![0usize; edges.len()];
    let mut on_stack = vec![false; edges.len()];
    let mut on_a_cycle = vec![false; edges.len()];
    let mut stack = Vec::new();
    let mut walk: Vec<(usize, usize)> = Vec::new();
    let mut nodes_seen = 0;
    for root in 0..edges.len() {
        if order[root] != 0 {
            continue;
        }
        walk.push((root, 0));
        while let Some((node, next_edge)) = walk.last_mut() {
            let node = *node;
            if order[node] == 0 {
                nodes_seen += 1;
                order[node] = nodes_seen;
                lowest[node] = nodes_seen;
                stack.push(node);
                on_stack[node] = true;
            }
            if let Some(&target) = edges[node].get(*next_edge) {
                *next_edge += 1;
                if target == node {
                    on_a_cycle[node] = true;
                } else if order[target] == 0 {
                    walk.push((target, 0));
                } else if on_stack[target] {
                    lowest[node] = lowest[node].min(order[target]);
                }
                continue;
            }
            walk.pop();
            if let Some((parent, _)) = walk.last() {
                lowest[*parent] = lowest[*parent].min(lowest[node]);
            }
            if lowest[node] == order[node] {
                let start = stack
                    .iter()
                    .rposition(|member| *member == node)
                    .expect("a node is on the stack");
                let is_cycle = stack.len() - start > 1;
                for member in stack.drain(start..) {
                    on_stack[member] = false;
                    on_a_cycle[member] |= is_cycle;
                }
            }
        }
    }
    on_a_cycle
}
