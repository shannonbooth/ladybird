/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#![allow(unused, clippy::all)]
#![allow(gc_container_on_stack, reason = "the test is of the flow checks, which find what this one rejects too")]

use js::{Cell, DeferGc, Gc, GcRefCell, Vm};

fn collect() {
    // SAFETY: Stand-in.
    unsafe { js::gc::capi::gc_heap_allocate_cell() };
}

fn calls_collect() {
    collect();
}

fn defers() {
    // SAFETY: Stand-in.
    unsafe { js::gc::capi::gc_heap_defer_gc() };
}

fn read(cells: &[Gc<Cell>]) {}

fn read_after_collecting(cells: &[Gc<Cell>]) {
    collect();
}

pub fn live_across(cell: Gc<Cell>) {
    let cells = vec![cell];
    calls_collect(); //~ WARN `cells` keeps GC pointers in 'Vec<Gc<Cell>>'
    read(&cells);
}

pub fn dead_before(cell: Gc<Cell>) {
    let cells = vec![cell];
    read(&cells);
    calls_collect();
}

pub fn lent_to_a_collecting_call(cell: Gc<Cell>) {
    let cells = vec![cell];
    read_after_collecting(&cells); //~ WARN `cells` keeps GC pointers in 'Vec<Gc<Cell>>'
}

pub fn iterated_across(cell: Gc<Cell>) {
    let cells = vec![cell];
    for cell in cells.iter() {
        calls_collect(); //~ WARN `cells` keeps GC pointers in 'Vec<Gc<Cell>>'
    }
}

pub fn no_collection(cell: Gc<Cell>) {
    let cells = vec![cell];
    defers();
    read(&cells);
}

pub fn under_deferral(vm: &Vm, cell: Gc<Cell>) {
    let defer_gc = DeferGc(vm);
    let cells = vec![cell];
    calls_collect();
    read(&cells);
}

pub fn after_deferral(vm: &Vm, cell: Gc<Cell>) {
    let defer_gc = DeferGc(vm);
    let cells = vec![cell];
    calls_collect();
    drop(defer_gc); //~ WARN `cells` keeps GC pointers in 'Vec<Gc<Cell>>'
    read(&cells);
}

pub fn takes_deferral(_: &DeferGc<'_>, cell: Gc<Cell>) {
    let cells = vec![cell];
    calls_collect();
    read(&cells);
}

pub fn closure_that_collects(cell: Gc<Cell>) {
    let cells = vec![cell];
    [1, 2].iter().for_each(|_| collect()); //~ WARN `cells` keeps GC pointers in 'Vec<Gc<Cell>>'
    read(&cells);
}

pub fn closure_that_does_not(cell: Gc<Cell>) {
    let cells = vec![cell];
    [1, 2].iter().for_each(|_| defers());
    read(&cells);
}

pub fn borrow_across(state: &GcRefCell<Vec<u32>>) {
    let mut borrow = state.borrow_mut();
    calls_collect(); //~ WARN `borrow`, a mutable borrow of traced state
    borrow.push(1);
}

pub fn borrow_ended(state: &GcRefCell<Vec<u32>>) {
    {
        let mut borrow = state.borrow_mut();
        borrow.push(1);
    }
    calls_collect();
}

fn with_borrow<R>(state: &GcRefCell<Vec<u32>>, callback: impl FnOnce() -> R) -> R {
    let borrow = state.borrow_mut();
    callback() //~ WARN `borrow`, a mutable borrow of traced state
}

pub fn passes_a_collecting_closure(state: &GcRefCell<Vec<u32>>) {
    with_borrow(state, || collect());
}

pub fn passes_a_closure_that_does_not(state: &GcRefCell<Vec<u32>>) {
    with_borrow(state, || 1);
}
