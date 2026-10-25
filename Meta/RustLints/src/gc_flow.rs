/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//! Flow-sensitive checks on MIR: which functions may collect garbage, and what is still in use across the calls that
//! may. Where the type-based checks reject every variable that keeps GC pointers in unscanned heap memory, these only
//! reject the ones in use across a collection, and they find mutable borrows of traced state held across one.
//!
//! A call may collect if it calls into LibGC's allocation or collection, into C++ (any foreign function outside the
//! modules listed below), through a function pointer or a trait object, or into a function of the checked crates that
//! may, while no deferral is held. Functions are looked into as the instances a call makes of them, so that a closure
//! a caller passes is known where it is called. Calls into other crates do not collect, unless they get a closure, a
//! function or a `dyn Fn` that may.

use rustc_data_structures::fx::{FxHashMap, FxHashSet};
use rustc_hir::HirId;
use rustc_hir::def::DefKind;
use rustc_hir::def_id::{DefId, LocalDefId};
use rustc_index::bit_set::DenseBitSet;
use rustc_lint::{LateContext, Lint};
use rustc_middle::mir::visit::{MutatingUseContext, NonMutatingUseContext, NonUseContext, PlaceContext, Visitor};
use rustc_middle::mir::{
    BasicBlock, Body, ClearCrossCrate, Local, Location, Operand, Place, ProjectionElem, Rvalue, StatementKind,
    TerminatorKind, VarDebugInfoContents,
};
use rustc_middle::ty::print::with_forced_trimmed_paths;
use rustc_middle::ty::{
    self, EarlyBinder, GenericArgs, GenericArgsRef, Instance, InstanceKind, Ty, TyCtxt, TypeVisitableExt, TypingEnv,
};
use rustc_session::declare_lint;
use rustc_span::{Span, Spanned};

use crate::gc_lints::{GcLints, Kind};

declare_lint! {
    /// A variable that keeps GC pointers in heap memory the collector does not scan is still in use after a call that
    /// may collect garbage, which can free its cells first.
    pub GC_CONTAINER_LIVE_ACROSS_COLLECTION,
    Warn,
    "GC pointers in unscanned heap memory in use across a call that may collect"
}

declare_lint! {
    /// A mutable borrow of traced state, a `GcRefMut` or `MarkedValuesMut`, is held across a call that may collect
    /// garbage. A collection that finds the state borrowed stops the process.
    pub GC_BORROW_ACROSS_COLLECTION,
    Warn,
    "mutable borrow of traced state held across a call that may collect"
}

/// The crates whose functions the checks look into, the ones Meta/RustLints/run.sh checks.
const ANALYZED_CRATES: &[&str] = &["libjs_rust", "libweb_bindings_rust", "libweb_rust"];

/// The modules whose foreign functions do not call back into the engine: ICU, libm, the allocator, executable memory,
/// LeakSanitizer and the disassembler.
const NON_COLLECTING_FOREIGN_MODULES: &[&str] = &[
    "libjs_rust::unicode",
    "libjs_rust::runtime::math_object",
    "libjs_rust::runtime::value_storage",
    "libjs_rust::jit::executable_memory",
    "libjs_rust::jit::entry_table",
    "libjs_rust::gc::class",
    "libjs_rust::bytecode::dump",
];

/// LibGC's functions that may collect garbage. The rest of its C interface does not.
const COLLECTING_GC_FUNCTIONS: &[&str] = &[
    "gc_heap_allocate_cell",
    "gc_heap_collect_garbage",
    "gc_heap_destroy",
    "gc_heap_undefer_gc",
];

/// A function as a call makes it: an instance, and the typing environment its body is checked in. One whose arguments
/// are not all known is its generic body, where calls that depend on its parameters can be anything.
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
struct Context<'tcx> {
    instance: Instance<'tcx>,
    typing_env: TypingEnv<'tcx>,
}

/// What a site of a body may do.
enum Effect<'tcx> {
    /// Collect garbage itself.
    Collects,
    /// Call these functions, which may collect.
    Calls(Vec<Context<'tcx>>),
}

/// A call, or the end of a deferral, that may collect garbage.
struct Site<'tcx> {
    location: Location,
    span: Span,
    effect: Effect<'tcx>,
    /// Whether a deferral is held, so that nothing collects.
    deferred: bool,
    /// The locals the terminator moves out of, which the callee owns from then on.
    moved: Vec<Local>,
    /// The local the call writes its result to, which nothing holds across the call.
    destination: Option<Local>,
}

#[derive(Default)]
pub(crate) struct FlowState<'tcx> {
    /// Whether each function may collect, once the fixed point is known for it.
    may_collect: FxHashMap<Context<'tcx>, bool>,
    /// The instances of local functions checked so far.
    checked: FxHashSet<Context<'tcx>>,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Event {
    /// The whole local is written.
    Define,
    /// The local is read or written in part.
    Use,
    /// The whole local is moved out of.
    Move,
    /// The local is borrowed.
    Borrow,
    /// The local is dropped.
    Drop,
    /// The local's storage ends.
    StorageDead,
}

#[derive(Default)]
struct Events(Vec<(Local, Event)>);

impl<'tcx> Visitor<'tcx> for Events {
    fn visit_place(&mut self, place: &Place<'tcx>, context: PlaceContext, _: Location) {
        let whole = place.projection.is_empty();
        let event = match context {
            PlaceContext::MutatingUse(
                MutatingUseContext::Store | MutatingUseContext::Call | MutatingUseContext::AsmOutput,
            ) if whole => Event::Define,
            PlaceContext::MutatingUse(MutatingUseContext::Drop) if whole => Event::Drop,
            PlaceContext::NonMutatingUse(NonMutatingUseContext::Move) if whole => Event::Move,
            PlaceContext::NonMutatingUse(NonMutatingUseContext::SharedBorrow | NonMutatingUseContext::RawBorrow)
            | PlaceContext::MutatingUse(MutatingUseContext::Borrow | MutatingUseContext::RawBorrow) => Event::Borrow,
            PlaceContext::NonUse(_) => return,
            _ => Event::Use,
        };
        self.0.push((place.local, event));
        for element in place.projection {
            if let ProjectionElem::Index(index) = element {
                self.0.push((index, Event::Use));
            }
        }
    }

    fn visit_local(&mut self, local: Local, context: PlaceContext, _: Location) {
        if context == PlaceContext::NonUse(NonUseContext::StorageDead) {
            self.0.push((local, Event::StorageDead));
        }
    }
}

/// The events of the statement or terminator at `location`, in order. A call's destination is written last.
fn events_at(body: &Body<'_>, location: Location) -> Vec<(Local, Event)> {
    let mut events = Events::default();
    let block = &body.basic_blocks[location.block];
    match block.statements.get(location.statement_index) {
        Some(statement) => events.visit_statement(statement, location),
        None => {
            let terminator = block.terminator();
            if let TerminatorKind::Call {
                func,
                args,
                destination,
                ..
            } = &terminator.kind
            {
                events.visit_operand(func, location);
                for argument in args.iter() {
                    events.visit_operand(&argument.node, location);
                }
                events.visit_place(
                    destination,
                    PlaceContext::MutatingUse(MutatingUseContext::Call),
                    location,
                );
            } else {
                events.visit_terminator(terminator, location);
            }
        }
    }
    events.0
}

fn locations_of(body: &Body<'_>, block: BasicBlock) -> impl Iterator<Item = Location> {
    (0..=body.basic_blocks[block].statements.len()).map(move |statement_index| Location { block, statement_index })
}

/// The successors of a block that are not cleanup blocks, which only run while unwinding.
fn successors(body: &Body<'_>, block: BasicBlock) -> Vec<BasicBlock> {
    body.basic_blocks[block]
        .terminator()
        .successors()
        .filter(|&successor| !body.basic_blocks[successor].is_cleanup)
        .collect()
}

/// A forward analysis of which of `tracked` are initialized: on some path (`must` false) or on every path (`must`
/// true). Returns the state at the start of each block.
fn initialized(
    body: &Body<'_>,
    tracked: &DenseBitSet<Local>,
    at_entry: &DenseBitSet<Local>,
    must: bool,
) -> Vec<DenseBitSet<Local>> {
    let count = body.local_decls.len();
    let mut entry_states: Vec<DenseBitSet<Local>> = (0..body.basic_blocks.len())
        .map(|_| {
            let mut state = DenseBitSet::new_empty(count);
            if must {
                state.union(tracked);
            }
            state
        })
        .collect();
    entry_states[0] = at_entry.clone();
    let mut changed = true;
    while changed {
        changed = false;
        for (block, data) in body.basic_blocks.iter_enumerated() {
            if data.is_cleanup {
                continue;
            }
            let mut state = entry_states[block.index()].clone();
            for location in locations_of(body, block) {
                transfer_initialized(body, location, tracked, &mut state);
            }
            for successor in successors(body, block) {
                if successor.index() == 0 {
                    continue;
                }
                let entry = &mut entry_states[successor.index()];
                let before = entry.clone();
                if must {
                    entry.intersect(&state);
                } else {
                    entry.union(&state);
                }
                changed |= *entry != before;
            }
        }
    }
    entry_states
}

fn transfer_initialized(
    body: &Body<'_>,
    location: Location,
    tracked: &DenseBitSet<Local>,
    state: &mut DenseBitSet<Local>,
) {
    for (local, event) in events_at(body, location) {
        if !tracked.contains(local) {
            continue;
        }
        match event {
            Event::Define => {
                state.insert(local);
            }
            Event::Move | Event::Drop | Event::StorageDead => {
                state.remove(local);
            }
            Event::Use | Event::Borrow => {}
        }
    }
}

/// A backward analysis of which of `tracked` are read later, where drops are no reads. Returns the state at the start
/// of each block.
fn live(body: &Body<'_>, tracked: &DenseBitSet<Local>) -> Vec<DenseBitSet<Local>> {
    let count = body.local_decls.len();
    let mut entry_states: Vec<DenseBitSet<Local>> = (0..body.basic_blocks.len())
        .map(|_| DenseBitSet::new_empty(count))
        .collect();
    let mut changed = true;
    while changed {
        changed = false;
        for (block, data) in body.basic_blocks.iter_enumerated().rev() {
            if data.is_cleanup {
                continue;
            }
            let mut state = DenseBitSet::new_empty(count);
            for successor in successors(body, block) {
                state.union(&entry_states[successor.index()]);
            }
            let locations: Vec<Location> = locations_of(body, block).collect();
            for &location in locations.iter().rev() {
                transfer_live(body, location, tracked, &mut state);
            }
            if state != entry_states[block.index()] {
                entry_states[block.index()] = state;
                changed = true;
            }
        }
    }
    entry_states
}

fn transfer_live(body: &Body<'_>, location: Location, tracked: &DenseBitSet<Local>, state: &mut DenseBitSet<Local>) {
    for (local, event) in events_at(body, location).into_iter().rev() {
        if !tracked.contains(local) {
            continue;
        }
        match event {
            Event::Define => {
                state.remove(local);
            }
            Event::Use | Event::Move | Event::Borrow => {
                state.insert(local);
            }
            Event::Drop | Event::StorageDead => {}
        }
    }
}

/// For each of `containers`, the locals that may hold a reference derived from a borrow of it: borrows of it or of
/// what such a reference points to, copies of them, values built from them, and the results of calls that get one and
/// return something that borrows.
fn derived_references(body: &Body<'_>, containers: &DenseBitSet<Local>) -> FxHashMap<Local, DenseBitSet<Local>> {
    let count = body.local_decls.len();
    let mut derived = FxHashMap::default();
    for container in containers.iter() {
        let mut references = DenseBitSet::new_empty(count);
        let reaches = |references: &DenseBitSet<Local>, local: Local| local == container || references.contains(local);
        let mut changed = true;
        while changed {
            changed = false;
            for data in body.basic_blocks.iter() {
                for statement in &data.statements {
                    let StatementKind::Assign(assignment) = &statement.kind else {
                        continue;
                    };
                    let (place, rvalue) = &**assignment;
                    let from = match rvalue {
                        Rvalue::Ref(_, _, borrowed) | Rvalue::RawPtr(_, borrowed) | Rvalue::CopyForDeref(borrowed) => {
                            reaches(&references, borrowed.local)
                        }
                        Rvalue::Use(operand, _) | Rvalue::Cast(_, operand, _) | Rvalue::Repeat(operand, _) => operand
                            .place()
                            .is_some_and(|operand| references.contains(operand.local)),
                        Rvalue::Aggregate(_, operands) => operands.iter().any(|operand| {
                            operand
                                .place()
                                .is_some_and(|operand| references.contains(operand.local))
                        }),
                        _ => false,
                    };
                    if from && references.insert(place.local) {
                        changed = true;
                    }
                }
                if let TerminatorKind::Call { args, destination, .. } = &data.terminator().kind
                    && args.iter().any(|argument| {
                        argument
                            .node
                            .place()
                            .is_some_and(|argument| references.contains(argument.local))
                    })
                    && body.local_decls[destination.local]
                        .ty
                        .walk()
                        .any(|argument| argument.as_region().is_some())
                    && references.insert(destination.local)
                {
                    changed = true;
                }
            }
        }
        references.remove(container);
        derived.insert(container, references);
    }
    derived
}

fn arguments_among(body: &Body<'_>, of: &DenseBitSet<Local>) -> DenseBitSet<Local> {
    let mut arguments = DenseBitSet::new_empty(body.local_decls.len());
    for local in body.args_iter() {
        if of.contains(local) {
            arguments.insert(local);
        }
    }
    arguments
}

fn local_name(body: &Body<'_>, local: Local) -> Option<String> {
    body.var_debug_info.iter().find_map(|info| match info.value {
        VarDebugInfoContents::Place(place) if place.local == local && place.projection.is_empty() => {
            Some(info.name.to_string())
        }
        _ => None,
    })
}

fn is_analyzed(tcx: TyCtxt<'_>, def_id: DefId) -> bool {
    def_id.is_local() || ANALYZED_CRATES.contains(&tcx.crate_name(def_id.krate).as_str())
}

fn foreign_function_collects(tcx: TyCtxt<'_>, def_id: DefId) -> bool {
    if tcx.crate_name(def_id.krate).as_str() != "libjs_rust" {
        return true;
    }
    let path = format!("libjs_rust::{}", tcx.def_path_str(tcx.parent(def_id)));
    if path.ends_with("::gc::capi") {
        return COLLECTING_GC_FUNCTIONS.contains(&tcx.item_name(def_id).as_str());
    }
    !NON_COLLECTING_FOREIGN_MODULES
        .iter()
        .any(|module| path == *module || path.starts_with(&format!("{module}::")))
}

impl<'tcx> Context<'tcx> {
    fn mono(tcx: TyCtxt<'tcx>, def_id: DefId) -> Self {
        Self {
            instance: Instance::mono(tcx, def_id),
            typing_env: TypingEnv::fully_monomorphized(),
        }
    }

    /// The instance a call in this body makes: fully known if its arguments are, or else the callee's generic body.
    fn of(tcx: TyCtxt<'tcx>, instance: Instance<'tcx>) -> Self {
        if !instance.args.has_param() {
            return Self {
                instance,
                typing_env: TypingEnv::fully_monomorphized(),
            };
        }
        let def_id = instance.def_id();
        Self {
            instance: Instance::new_raw(def_id, GenericArgs::identity_for_item(tcx, def_id)),
            typing_env: TypingEnv::post_analysis(tcx, def_id),
        }
    }

    fn body(self, tcx: TyCtxt<'tcx>) -> Option<&'tcx Body<'tcx>> {
        let InstanceKind::Item(def_id) = self.instance.def else {
            return None;
        };
        if !is_analyzed(tcx, def_id)
            || !matches!(tcx.def_kind(def_id), DefKind::Fn | DefKind::AssocFn | DefKind::Closure)
            || !tcx.is_mir_available(def_id)
        {
            return None;
        }
        Some(tcx.instance_mir(self.instance.def))
    }

    fn instantiate(self, tcx: TyCtxt<'tcx>, ty: Ty<'tcx>) -> Ty<'tcx> {
        self.instance
            .instantiate_mir_and_normalize_erasing_regions(tcx, self.typing_env, EarlyBinder::bind(ty))
    }
}

impl<'tcx> GcLints<'tcx> {
    /// Whether values of `ty` hold a deferral: a `DeferGc`, a `Deferred` or a type tied to one.
    fn holds_deferral(&mut self, tcx: TyCtxt<'tcx>, ty: Ty<'tcx>, by_reference: bool) -> bool {
        let ty = match ty.kind() {
            ty::Ref(_, pointee, _) if by_reference => *pointee,
            _ => ty,
        };
        matches!(ty.kind(), ty::Adt(adt, _)
            if self.defers_collections_types.contains(&adt.did()) || self.is_deferral_scoped(tcx, adt.did()))
    }

    /// Whether values of `ty` are mutable borrows of traced state, which nothing may collect while they live.
    fn forbids_collections(&self, ty: Ty<'tcx>) -> bool {
        matches!(ty.kind(), ty::Adt(adt, _) if self.forbids_collections_types.contains(&adt.did()))
    }

    /// The locals that hold a deferral while initialized: those by value, and arguments by reference too, whose
    /// deferral outlives the body.
    fn deferral_holders(&mut self, tcx: TyCtxt<'tcx>, context: Context<'tcx>, body: &Body<'tcx>) -> DenseBitSet<Local> {
        let mut holders = DenseBitSet::new_empty(body.local_decls.len());
        for (local, declaration) in body.local_decls.iter_enumerated() {
            let is_argument = (1..=body.arg_count).contains(&local.index());
            if self.holds_deferral(tcx, context.instantiate(tcx, declaration.ty), is_argument) {
                holders.insert(local);
            }
        }
        holders
    }

    /// The functions among the arguments of a call to a callee that is not looked into, which it may call, or `None`
    /// if one is a function pointer or a `dyn Fn`, which may do anything.
    fn callables_among(&self, tcx: TyCtxt<'tcx>, arguments: &[Ty<'tcx>]) -> Option<Vec<(DefId, GenericArgsRef<'tcx>)>> {
        let mut callables = Vec::new();
        for &argument in arguments {
            let mut ty = argument;
            while let ty::Ref(_, pointee, _) = ty.kind() {
                ty = *pointee;
            }
            match ty.kind() {
                ty::Closure(def_id, args) | ty::FnDef(def_id, args) => callables.push((*def_id, *args)),
                ty::FnPtr(..) => return None,
                ty::Dynamic(predicates, ..)
                    if predicates
                        .principal_def_id()
                        .is_some_and(|trait_id| tcx.is_fn_trait(trait_id)) =>
                {
                    return None;
                }
                _ => {}
            }
        }
        Some(callables)
    }

    /// What the call to `func` with `args` in `context` may do, or `None` if it does not collect.
    fn call_effect(
        &mut self,
        tcx: TyCtxt<'tcx>,
        context: Context<'tcx>,
        body: &Body<'tcx>,
        func: &Operand<'tcx>,
        args: &[Spanned<Operand<'tcx>>],
    ) -> Option<Effect<'tcx>> {
        let ty::FnDef(callee, generic_args) = *context.instantiate(tcx, func.ty(body, tcx)).kind() else {
            return Some(Effect::Collects);
        };
        if tcx.is_foreign_item(callee) {
            return foreign_function_collects(tcx, callee).then_some(Effect::Collects);
        }
        let Ok(Some(instance)) = Instance::try_resolve(tcx, context.typing_env, callee, generic_args) else {
            // NB: A method of a trait on a type parameter can be anything.
            return Some(Effect::Collects);
        };
        match instance.def {
            InstanceKind::Item(def_id) if is_analyzed(tcx, def_id) => {
                Some(Effect::Calls(vec![Context::of(tcx, instance)]))
            }
            InstanceKind::Virtual(..) | InstanceKind::FnPtrShim(..) => Some(Effect::Collects),
            InstanceKind::DropGlue(..) => None,
            _ => {
                let argument_types: Vec<Ty<'tcx>> = args
                    .iter()
                    .map(|argument| context.instantiate(tcx, argument.node.ty(body, tcx)))
                    .collect();
                let Some(callables) = self.callables_among(tcx, &argument_types) else {
                    return Some(Effect::Collects);
                };
                let mut called = Vec::new();
                for (def_id, callable_args) in callables {
                    if !is_analyzed(tcx, def_id) {
                        continue;
                    }
                    let instance = if tcx.is_closure_like(def_id) {
                        Instance::new_raw(def_id, callable_args)
                    } else {
                        match Instance::try_resolve(tcx, context.typing_env, def_id, callable_args) {
                            Ok(Some(instance)) => instance,
                            _ => return Some(Effect::Collects),
                        }
                    };
                    called.push(Context::of(tcx, instance));
                }
                (!called.is_empty()).then_some(Effect::Calls(called))
            }
        }
    }

    /// The sites of the body of `context` that may collect garbage, with whether a deferral is held at each.
    fn sites(&mut self, tcx: TyCtxt<'tcx>, context: Context<'tcx>, body: &Body<'tcx>) -> Vec<Site<'tcx>> {
        let holders = self.deferral_holders(tcx, context, body);
        let held_at_entry = arguments_among(body, &holders);
        let held = initialized(body, &holders, &held_at_entry, true);
        let mut sites = Vec::new();
        for (block, data) in body.basic_blocks.iter_enumerated() {
            if data.is_cleanup {
                continue;
            }
            let terminator_location = body.terminator_loc(block);
            let mut state = held[block.index()].clone();
            for location in locations_of(body, block) {
                if location != terminator_location {
                    transfer_initialized(body, location, &holders, &mut state);
                }
            }
            let terminator = data.terminator();
            let moved: Vec<Local> = events_at(body, terminator_location)
                .into_iter()
                .filter(|&(_, event)| event == Event::Move)
                .map(|(local, _)| local)
                .collect();
            let (effect, destination, deferred) = match &terminator.kind {
                TerminatorKind::Call {
                    func,
                    args,
                    destination,
                    ..
                } => {
                    // NB: A deferral moved into a call ends in it, as it does in mem::drop().
                    let ends_deferral = moved.iter().any(|&local| {
                        matches!(context.instantiate(tcx, body.local_decls[local].ty).kind(),
                            ty::Adt(adt, _) if self.kind(tcx, adt.did()) == Kind::DeferGc)
                    });
                    let effect = match self.call_effect(tcx, context, body, func, args) {
                        _ if ends_deferral => Effect::Collects,
                        Some(effect) => effect,
                        None => continue,
                    };
                    let mut held_during = state.clone();
                    for &local in &moved {
                        held_during.remove(local);
                    }
                    (effect, destination.as_local(), !held_during.is_empty())
                }
                // NB: Ending a deferral collects the garbage it held off, unless an outer one is still held.
                TerminatorKind::Drop { place, .. }
                    if place.projection.is_empty()
                        && matches!(context.instantiate(tcx, body.local_decls[place.local].ty).kind(),
                            ty::Adt(adt, _) if self.kind(tcx, adt.did()) == Kind::DeferGc) =>
                {
                    let mut others = state.clone();
                    others.remove(place.local);
                    (Effect::Collects, None, !others.is_empty())
                }
                _ => continue,
            };
            sites.push(Site {
                location: terminator_location,
                span: terminator.source_info.span,
                effect,
                deferred,
                moved,
                destination,
            });
        }
        sites
    }

    /// Whether `context` may collect garbage, found by a fixed point over the functions it reaches.
    fn may_collect(&mut self, tcx: TyCtxt<'tcx>, context: Context<'tcx>) -> bool {
        if let Some(&known) = self.flow.may_collect.get(&context) {
            return known;
        }
        // NB: Every function reached gets its effects first, and then the fixed point is found for all of them.
        let mut effects: FxHashMap<Context<'tcx>, (bool, Vec<Context<'tcx>>)> = FxHashMap::default();
        let mut pending = vec![context];
        while let Some(function) = pending.pop() {
            if effects.contains_key(&function) || self.flow.may_collect.contains_key(&function) {
                continue;
            }
            let Some(body) = function.body(tcx) else {
                effects.insert(function, (false, Vec::new()));
                continue;
            };
            let mut collects = false;
            let mut callees = Vec::new();
            for site in self.sites(tcx, function, body) {
                if site.deferred {
                    continue;
                }
                match site.effect {
                    Effect::Collects => collects = true,
                    Effect::Calls(called) => callees.extend(called),
                }
            }
            pending.extend(callees.iter().copied());
            effects.insert(function, (collects, callees));
        }
        let mut result: FxHashMap<Context<'tcx>, bool> = effects
            .iter()
            .map(|(&function, &(collects, _))| (function, collects))
            .collect();
        let mut changed = true;
        while changed {
            changed = false;
            for (function, (_, callees)) in &effects {
                if result[function] {
                    continue;
                }
                let reaches = callees.iter().any(|callee| {
                    self.flow
                        .may_collect
                        .get(callee)
                        .or_else(|| result.get(callee))
                        .copied()
                        .unwrap_or(false)
                });
                if reaches {
                    result.insert(*function, true);
                    changed = true;
                }
            }
        }
        self.flow.may_collect.extend(result);
        self.flow.may_collect[&context]
    }

    fn site_collects(&mut self, tcx: TyCtxt<'tcx>, site: &Site<'tcx>) -> bool {
        if site.deferred {
            return false;
        }
        match &site.effect {
            Effect::Collects => true,
            Effect::Calls(callees) => callees.iter().any(|&callee| self.may_collect(tcx, callee)),
        }
    }

    fn lint_root(body: &Body<'tcx>, location: Location, fallback: HirId) -> HirId {
        let scope = body.source_info(location).scope;
        match &body.source_scopes[scope].local_data {
            ClearCrossCrate::Set(data) => data.lint_root,
            ClearCrossCrate::Clear => fallback,
        }
    }

    /// Checks what the body of the local function `def_id` holds across the calls in it that may collect garbage, and
    /// the bodies of the instances of local generic functions it makes, where the closures it passes are known.
    pub(crate) fn check_flow(&mut self, cx: &LateContext<'tcx>, def_id: LocalDefId) {
        let tcx = cx.tcx;
        let def_id = def_id.to_def_id();
        // NB: A generic function is checked as the instances its callers make of it.
        if tcx.generics_of(def_id).requires_monomorphization(tcx) {
            return;
        }
        let mut pending = vec![Context::mono(tcx, def_id)];
        while let Some(context) = pending.pop() {
            if !self.flow.checked.insert(context) {
                continue;
            }
            pending.extend(self.check_flow_of(cx, context));
        }
    }

    /// Checks one body, and returns the instances of local generic functions it calls.
    fn check_flow_of(&mut self, cx: &LateContext<'tcx>, context: Context<'tcx>) -> Vec<Context<'tcx>> {
        let tcx = cx.tcx;
        let Some(body) = context.body(tcx) else {
            return Vec::new();
        };
        let Some(local) = context.instance.def_id().as_local() else {
            return Vec::new();
        };
        let fallback = tcx.local_def_id_to_hir_id(tcx.typeck_root_def_id(local.to_def_id()).expect_local());
        let sites = self.sites(tcx, context, body);
        let generic_callees: Vec<Context<'tcx>> = sites
            .iter()
            .filter_map(|site| match &site.effect {
                Effect::Calls(callees) => Some(callees.clone()),
                Effect::Collects => None,
            })
            .flatten()
            .filter(|callee| {
                callee.instance.def_id().is_local()
                    && !callee.instance.args.is_empty()
                    && callee.typing_env == TypingEnv::fully_monomorphized()
            })
            .collect();

        let count = body.local_decls.len();
        let mut containers = DenseBitSet::new_empty(count);
        let mut guards = DenseBitSet::new_empty(count);
        let mut container_types = FxHashMap::default();
        for (local, declaration) in body.local_decls.iter_enumerated() {
            let ty = context.instantiate(tcx, declaration.ty);
            // NB: A temporary of a container is a field on its way into its value, which the type checks see where
            //     that is held.
            let is_variable = (1..=body.arg_count).contains(&local.index()) || local_name(body, local).is_some();
            if self.forbids_collections(ty) {
                guards.insert(local);
            } else if is_variable && let Some(container) = self.unscanned_container(cx, ty) {
                containers.insert(local);
                container_types.insert(local, container);
            }
        }
        if containers.is_empty() && guards.is_empty() {
            return generic_callees;
        }
        // NB: A borrowed container is in use for as long as a reference derived from the borrow is.
        let derived = derived_references(body, &containers);
        let mut tracked = containers.clone();
        for references in derived.values() {
            tracked.union(references);
        }
        let initialized_at_entry = arguments_among(body, &guards);
        let initialized_states = initialized(body, &guards, &initialized_at_entry, false);
        let live_states = live(body, &tracked);

        let instance_note = if context.instance.args.is_empty() {
            String::new()
        } else {
            with_forced_trimmed_paths!(format!(" (in {})", context.instance))
        };
        let mut reported = FxHashSet::default();
        for site in &sites {
            if !self.site_collects(tcx, site) {
                continue;
            }
            let block = site.location.block;
            let mut initialized = initialized_states[block.index()].clone();
            for location in locations_of(body, block) {
                if location == site.location {
                    break;
                }
                transfer_initialized(body, location, &guards, &mut initialized);
            }
            let mut live_after = DenseBitSet::new_empty(count);
            for successor in successors(body, block) {
                live_after.union(&live_states[successor.index()]);
            }
            if let Some(destination) = site.destination {
                live_after.remove(destination);
            }
            let used_by_call: FxHashSet<Local> = events_at(body, site.location)
                .into_iter()
                .filter(|&(_, event)| matches!(event, Event::Use | Event::Move))
                .map(|(local, _)| local)
                .collect();
            let mut held: Vec<Local> = initialized.iter().filter(|local| !site.moved.contains(local)).collect();
            for container in containers.iter() {
                if site.moved.contains(&container) {
                    continue;
                }
                let references = &derived[&container];
                let in_use_after = live_after.contains(container) || references.iter().any(|r| live_after.contains(r));
                let lent_to_call = references.iter().any(|r| used_by_call.contains(&r));
                if in_use_after || lent_to_call {
                    held.push(container);
                }
            }
            for local in held {
                if !reported.insert((site.location, local)) {
                    continue;
                }
                let what = match local_name(body, local) {
                    Some(name) => format!("`{name}`"),
                    None => "a temporary".to_owned(),
                };
                let (lint, message): (&'static Lint, String) = if guards.contains(local) {
                    (
                        GC_BORROW_ACROSS_COLLECTION,
                        with_forced_trimmed_paths!(format!(
                            "{what}, a mutable borrow of traced state of type '{}', is held across this call, which \
                             may collect garbage{instance_note}",
                            context.instantiate(tcx, body.local_decls[local].ty)
                        )),
                    )
                } else {
                    (
                        GC_CONTAINER_LIVE_ACROSS_COLLECTION,
                        with_forced_trimmed_paths!(format!(
                            "{what} keeps GC pointers in '{}', which the collector does not scan, across this call, \
                             which may collect garbage{instance_note}",
                            container_types[&local]
                        )),
                    )
                };
                let hir_id = Self::lint_root(body, site.location, fallback);
                self.emit(cx, lint, hir_id, site.span, message);
            }
        }
        generic_callees
    }
}
