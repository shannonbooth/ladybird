/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashTable.h>
#include <AK/NumericLimits.h>
#include <AK/QuickSort.h>
#include <LibWebView/CanonicalNavigable.h>
#include <LibWebView/SessionHistory.h>

namespace WebView {

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#getting-all-used-history-steps
static Vector<i32> get_all_used_history_steps(Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>> const& traversable_session_history_entries)
{
    // 1. Assert: this is running within traversable's session history traversal queue.

    // 2. Let steps be an empty ordered set of non-negative integers.
    OrderedHashTable<i32> steps;

    // 3. Let entryLists be the ordered set « traversable's session history entries ».
    Vector<Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>> const*> entry_lists { &traversable_session_history_entries };

    // 4. For each entryList of entryLists:
    while (!entry_lists.is_empty()) {
        auto const* entry_list = entry_lists.take_first();

        // 1. For each entry of entryList:
        for (auto const& entry : *entry_list) {
            // 1. Append entry's step to steps.
            steps.set(entry->step);

            // 2. For each nestedHistory of entry's document state's nested histories, append
            //    nestedHistory's entries list to entryLists.
            for (auto const& nested_history : entry->document_state->nested_histories)
                entry_lists.append(&nested_history.entries);
        }
    }

    // 5. Return steps, sorted.
    auto sorted_steps = steps.values();
    quick_sort(sorted_steps);
    return sorted_steps;
}

static bool entries_have_nested_histories(Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>> const& entries)
{
    for (auto const& entry : entries) {
        if (!entry->document_state->nested_histories.is_empty())
            return true;
    }
    return false;
}

static Optional<size_t> top_level_entry_index_for_step(Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>> const& entries, i32 step)
{
    Optional<size_t> result;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i]->step > step)
            break;
        result = i;
    }
    return result;
}

static Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>> entries_from_descriptors(Vector<Web::HTML::SessionHistoryEntryDescriptor> const& descriptors)
{
    CanonicalSessionHistoryEntry::DocumentStates document_states;
    Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>> entries;
    entries.ensure_capacity(descriptors.size());
    for (auto const& descriptor : descriptors)
        entries.unchecked_append(CanonicalSessionHistoryEntry::create_from_descriptor(descriptor, document_states));
    return entries;
}

static bool steps_are_valid(Vector<i32> const& steps)
{
    Optional<i32> previous_step;
    for (auto const& step : steps) {
        if (step < 0)
            return false;
        if (previous_step.has_value() && step <= *previous_step)
            return false;
        previous_step = step;
    }
    return true;
}

static bool entries_are_valid(Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>> const& entries)
{
    Optional<i32> previous_step;
    for (auto const& entry : entries) {
        if (entry->step < 0)
            return false;
        if (previous_step.has_value() && entry->step <= *previous_step)
            return false;
        for (auto const& nested_history : entry->document_state->nested_histories) {
            if (!entries_are_valid(nested_history.entries))
                return false;
        }
        previous_step = entry->step;
    }
    return true;
}

// NB: Checked before the snapshot's entries are built, which recurses into its nested histories.
static bool nesting_depth_is_valid(Vector<Web::HTML::SessionHistoryEntryDescriptor> const& entries, size_t depth = 0)
{
    if (depth > MAX_NESTED_HISTORY_DEPTH)
        return false;

    for (auto const& entry : entries) {
        for (auto const& nested_history : entry.document_state.nested_histories) {
            if (!nesting_depth_is_valid(nested_history.entries, depth + 1))
                return false;
        }
    }
    return true;
}

static CanonicalSessionHistoryEntry* entry_for_step_in_entry_list(Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>> const& entries, i32 step)
{
    CanonicalSessionHistoryEntry* result = nullptr;
    for (auto const& entry : entries) {
        if (entry->step > step)
            break;
        result = entry.ptr();
    }
    return result;
}

// A step found only under an inactive nested sibling (off the greatest-step<=target path) is not reachable.
static bool active_path_reaches_step(CanonicalSessionHistoryEntry const& active_entry, i32 step)
{
    if (active_entry.step == step)
        return true;
    for (auto const& nested_history : active_entry.document_state->nested_histories) {
        auto const* active_nested_entry = entry_for_step_in_entry_list(nested_history.entries, step);
        if (active_nested_entry && active_path_reaches_step(*active_nested_entry, step))
            return true;
    }
    return false;
}

ErrorOr<void> validate_snapshot_is_restorable(Vector<Web::HTML::SessionHistoryEntryDescriptor> const& descriptors, Vector<i32> const& used_steps, size_t current_used_step_index)
{
    if (descriptors.is_empty() || used_steps.is_empty() || current_used_step_index >= used_steps.size() || !nesting_depth_is_valid(descriptors))
        return Error::from_string_literal("Session history snapshot is structurally invalid");

    auto entries = entries_from_descriptors(descriptors);
    if (!entries_are_valid(entries) || !steps_are_valid(used_steps) || get_all_used_history_steps(entries) != used_steps)
        return Error::from_string_literal("Session history snapshot is structurally invalid");

    for (auto step : used_steps) {
        auto top_level_entry_index = top_level_entry_index_for_step(entries, step);
        if (!top_level_entry_index.has_value() || !active_path_reaches_step(*entries[*top_level_entry_index], step))
            return Error::from_string_literal("Session history snapshot has a used step that is not reachable");
    }

    auto current_top_level_entry_index = top_level_entry_index_for_step(entries, used_steps[current_used_step_index]);
    if (!entries[*current_top_level_entry_index]->document_state->ever_populated)
        return Error::from_string_literal("Session history snapshot's current entry has no document state");

    return {};
}

static void clear_forward_session_history_entries(Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>>& entries, i32 step)
{
    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#clear-the-forward-session-history

    // 1. Assert: this is running within navigable's session history traversal queue.

    // 2. Let step be the navigable's current session history step.

    // 3. Let entryLists be the ordered set « navigable's session history entries ».
    Vector<Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>>*> entry_lists { &entries };

    // 4. For each entryList of entryLists:
    while (!entry_lists.is_empty()) {
        auto* entry_list = entry_lists.take_first();

        // 1. Remove every session history entry from entryList that has a step greater than step.
        entry_list->remove_all_matching([step](auto const& entry) {
            return entry->step > step;
        });

        // 2. For each entry of entryList:
        for (auto& entry : *entry_list) {
            // 1. For each nestedHistory of entry's document state's nested histories, append
            //    nestedHistory's entries list to entryLists.
            for (auto& nested_history : entry->document_state->nested_histories) {
                if (!entry_lists.contains_slow(&nested_history.entries))
                    entry_lists.append(&nested_history.entries);
            }
        }
    }
}

static void for_each_document_state(Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>> const& entries, HashTable<CanonicalDocumentState const*>& visited, Function<void(CanonicalDocumentState&)> const& callback)
{
    for (auto const& entry : entries) {
        auto& document_state = *entry->document_state;
        if (visited.set(&document_state) != HashSetResult::InsertedNewEntry)
            continue;
        callback(document_state);
        for (auto const& nested_history : document_state.nested_histories)
            for_each_document_state(nested_history.entries, visited, callback);
    }
}

TraversableSessionHistory::Checkpoint TraversableSessionHistory::checkpoint() const
{
    Checkpoint checkpoint {
        .entries = m_entries,
        .used_steps = m_used_steps,
        .current_used_step_index = m_current_used_step_index,
        .document_states = {},
    };
    HashTable<CanonicalDocumentState const*> visited;
    for_each_document_state(m_entries, visited, [&](CanonicalDocumentState& document_state) {
        auto values = CanonicalDocumentState::create(document_state.id);
        values->copy_values_from(document_state);
        checkpoint.document_states.append({ document_state, move(values) });
    });
    return checkpoint;
}

void TraversableSessionHistory::roll_back_to(Checkpoint checkpoint)
{
    for (auto const& [document_state, values] : checkpoint.document_states)
        document_state->copy_values_from(*values);
    m_entries = move(checkpoint.entries);
    m_used_steps = move(checkpoint.used_steps);
    m_current_used_step_index = checkpoint.current_used_step_index;
}

void TraversableSessionHistory::clear()
{
    m_entries.clear();
    m_used_steps.clear();
    m_current_used_step_index.clear();
}

bool TraversableSessionHistory::initialize_for_testing(Vector<Web::HTML::SessionHistoryEntryDescriptor> entries, Vector<i32> used_steps, size_t current_used_step_index)
{
    if (entries.is_empty() || current_used_step_index >= used_steps.size())
        return false;
    auto canonical_entries = entries_from_descriptors(entries);
    if (get_all_used_history_steps(canonical_entries) != used_steps)
        return false;

    m_entries = move(canonical_entries);
    m_used_steps = move(used_steps);
    m_current_used_step_index = current_used_step_index;
    return true;
}

void TraversableSessionHistory::initialize_with_initial_history_entry(NonnullRefPtr<CanonicalSessionHistoryEntry> initial_history_entry)
{
    m_entries.append(move(initial_history_entry));
    m_used_steps.append(0);
    m_current_used_step_index = 0;
}

static void assign_fresh_ids_to_restored_entries(Vector<Web::HTML::SessionHistoryEntryDescriptor>& entries, Function<Web::HTML::CrossProcessId()> const& allocate_cross_process_id, HashMap<Web::HTML::CrossProcessId, Web::HTML::CrossProcessId>& assigned_ids)
{
    for (auto& entry : entries) {
        entry.document_state.id = assigned_ids.ensure(entry.document_state.id, [&] { return allocate_cross_process_id(); });
        for (auto& nested_history : entry.document_state.nested_histories) {
            nested_history.id = assigned_ids.ensure(nested_history.id, [&] { return allocate_cross_process_id(); });
            assign_fresh_ids_to_restored_entries(nested_history.entries, allocate_cross_process_id, assigned_ids);
        }
    }
}

ErrorOr<void> TraversableSessionHistory::restore_from_ui_snapshot(Vector<Web::HTML::SessionHistoryEntryDescriptor> entries, Vector<i32> used_steps, size_t current_used_step_index, Function<Web::HTML::CrossProcessId()> allocate_cross_process_id)
{
    TRY(validate_snapshot_is_restorable(entries, used_steps, current_used_step_index));

    HashMap<Web::HTML::CrossProcessId, Web::HTML::CrossProcessId> assigned_ids;
    assign_fresh_ids_to_restored_entries(entries, allocate_cross_process_id, assigned_ids);
    m_entries = entries_from_descriptors(entries);
    m_used_steps = move(used_steps);
    m_current_used_step_index = current_used_step_index;
    return {};
}

void TraversableSessionHistory::mark_current_entry_reload_pending()
{
    auto current_top_level_entry_index = this->current_top_level_entry_index();
    if (!current_top_level_entry_index.has_value())
        return;

    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#reload
    // Set navigable's active session history entry's document state's reload
    // pending to true.
    m_entries[*current_top_level_entry_index]->document_state->reload_pending = true;
}

// Whether a document state is among its own nested histories' entries, which an entry naming one can make it.
static bool has_document_state_cycle(Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>> const& entries, Vector<CanonicalDocumentState const*>& ancestors)
{
    for (auto const& entry : entries) {
        auto const& document_state = *entry->document_state;
        if (ancestors.contains_slow(&document_state))
            return true;
        ancestors.append(&document_state);
        for (auto const& nested_history : document_state.nested_histories) {
            if (has_document_state_cycle(nested_history.entries, ancestors))
                return true;
        }
        ancestors.take_last();
    }
    return false;
}

static bool has_document_state_cycle(Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>> const& entries)
{
    Vector<CanonicalDocumentState const*> ancestors;
    return has_document_state_cycle(entries, ancestors);
}

Optional<i32> TraversableSessionHistory::append_nested_history(CanonicalNavigable const& parent_navigable, Web::HTML::CrossProcessId parent_document_state_id, Web::HTML::CrossProcessId child_navigable_id, NonnullRefPtr<CanonicalSessionHistoryEntry> history_entry)
{
    if (!m_current_used_step_index.has_value())
        return {};

    // https://html.spec.whatwg.org/multipage/document-sequences.html#create-a-new-child-navigable
    // These are steps 1-6 of the traversal steps appended by "create a new child navigable". WebContent supplies the
    // identity of parentDocState, whose live object it obtained from parentNavigable's active entry. The canonical
    // entry list supplies targetStepSHE and therefore owns the concrete step assigned here.
    auto current_step = m_used_steps[*m_current_used_step_index];

    // 2. Let parentNavigableEntries be the result of getting session history entries for parentNavigable.
    auto parent_entries = get_session_history_entries(parent_navigable);
    if (!parent_entries.has_value())
        return {};

    // 3. Let targetStepSHE be the first session history entry in parentNavigableEntries whose document state equals
    //    parentDocState.
    auto target_step_entry = parent_entries->find_if([&](auto const& entry) {
        return entry->document_state->id == parent_document_state_id;
    });
    if (target_step_entry == parent_entries->end())
        return {};

    auto target_step = (*target_step_entry)->step;
    auto& parent_document_state = *(*target_step_entry)->document_state;

    // Append nestedHistory to parentDocState's nested histories.
    auto existing_nested_history = parent_document_state.nested_histories.find_if([&](auto const& existing_nested_history) {
        return existing_nested_history.id == child_navigable_id;
    });
    if (existing_nested_history == parent_document_state.nested_histories.end()) {
        history_entry->step = target_step;
        parent_document_state.nested_histories.append({ .id = child_navigable_id, .entries = { move(history_entry) } });
        if (has_document_state_cycle(m_entries)) {
            parent_document_state.nested_histories.take_last();
            return {};
        }
    }

    m_used_steps = get_all_used_history_steps(m_entries);
    m_current_used_step_index = m_used_steps.find_first_index(current_step);
    VERIFY(m_current_used_step_index.has_value());
    return target_step;
}

bool TraversableSessionHistory::remove_nested_history(CanonicalNavigable const& parent_navigable, Web::HTML::CrossProcessId parent_document_state_id, Web::HTML::CrossProcessId child_navigable_id)
{
    if (!m_current_used_step_index.has_value())
        return false;

    // https://html.spec.whatwg.org/multipage/document-sequences.html#destroy-a-child-navigable
    // Let parentDocState be container's node navigable's active session history entry's document state. The live
    // parent entry was read before these traversal steps were appended, so use the reported stable document-state
    // identity instead of resolving the UI's current step again when the IPC request arrives.
    auto current_step = m_used_steps[*m_current_used_step_index];
    auto parent_entries = get_session_history_entries(parent_navigable);
    if (!parent_entries.has_value())
        return false;
    auto parent_entry = parent_entries->find_if([&](auto const& entry) { return entry->document_state->id == parent_document_state_id; });
    if (parent_entry == parent_entries->end())
        return false;

    // Remove the nested history from parentDocState's nested histories whose id equals navigable's id.
    (*parent_entry)->document_state->nested_histories.remove_all_matching([child_navigable_id](auto const& nested_history) {
        return nested_history.id == child_navigable_id;
    });

    m_used_steps = get_all_used_history_steps(m_entries);
    auto used_current_step = current_step;
    if (!m_used_steps.contains_slow(current_step)) {
        for (auto used_step : m_used_steps) {
            if (used_step > current_step)
                break;
            used_current_step = used_step;
        }
    }
    m_current_used_step_index = m_used_steps.find_first_index(used_current_step);
    VERIFY(m_current_used_step_index.has_value());
    return true;
}

static bool append_or_replace_entry(Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>>& entries, NonnullRefPtr<CanonicalSessionHistoryEntry> entry, CanonicalSessionHistoryEntry const* entry_to_replace)
{
    if (!entry_to_replace) {
        entries.append(move(entry));
        return true;
    }

    auto existing_entry = entries.find_if([&](auto const& entry) { return entry.ptr() == entry_to_replace; });
    if (existing_entry == entries.end())
        return false;

    entry->step = (*existing_entry)->step;
    *existing_entry = move(entry);
    return true;
}

bool TraversableSessionHistory::clear_the_forward_session_history()
{
    auto current_step = this->current_step();
    if (!current_step.has_value())
        return false;

    clear_forward_session_history_entries(m_entries, *current_step);

    m_used_steps = get_all_used_history_steps(m_entries);
    m_current_used_step_index = m_used_steps.find_first_index(*current_step);
    return m_current_used_step_index.has_value();
}

bool TraversableSessionHistory::append_or_replace_entry_for_navigable(CanonicalNavigable const& navigable, NonnullRefPtr<CanonicalSessionHistoryEntry> entry, CanonicalSessionHistoryEntry const* entry_to_replace)
{
    auto current_step = this->current_step();
    if (!current_step.has_value())
        return false;

    auto target_entries = get_session_history_entries(navigable);
    if (!target_entries.has_value())
        return false;
    if (!append_or_replace_entry(*target_entries, move(entry), entry_to_replace))
        return false;
    if (has_document_state_cycle(m_entries))
        return false;

    m_used_steps = get_all_used_history_steps(m_entries);
    m_current_used_step_index = m_used_steps.find_first_index(*current_step);
    return m_current_used_step_index.has_value();
}

Optional<i32> TraversableSessionHistory::push_session_history_entry(CanonicalNavigable const& navigable, NonnullRefPtr<CanonicalSessionHistoryEntry> entry)
{
    auto current_step = this->current_step();
    if (!current_step.has_value())
        return {};
    VERIFY(*current_step < NumericLimits<i32>::max());
    auto target_step = *current_step + 1;

    auto checkpoint = this->checkpoint();
    entry->step = target_step;
    if (!clear_the_forward_session_history() || !append_or_replace_entry_for_navigable(navigable, move(entry), nullptr)) {
        roll_back_to(move(checkpoint));
        return {};
    }
    return target_step;
}

bool TraversableSessionHistory::replace_session_history_entry(CanonicalNavigable const& navigable, CanonicalSessionHistoryEntry const& entry_to_replace, NonnullRefPtr<CanonicalSessionHistoryEntry> entry)
{
    auto checkpoint = this->checkpoint();
    if (append_or_replace_entry_for_navigable(navigable, move(entry), &entry_to_replace))
        return true;
    roll_back_to(move(checkpoint));
    return false;
}

Optional<size_t> TraversableSessionHistory::current_top_level_entry_index() const
{
    if (!m_current_used_step_index.has_value())
        return {};
    return top_level_entry_index_for_step(m_entries, m_used_steps[*m_current_used_step_index]);
}

Vector<Web::HTML::SessionHistoryEntryDescriptor> TraversableSessionHistory::entries() const
{
    Vector<Web::HTML::SessionHistoryEntryDescriptor> entries;
    entries.ensure_capacity(m_entries.size());
    for (auto const& entry : m_entries)
        entries.unchecked_append(entry->descriptor());
    return entries;
}

Vector<i32> TraversableSessionHistory::used_steps() const
{
    return m_used_steps;
}

bool TraversableSessionHistory::can_go_back() const
{
    return m_current_used_step_index.has_value() && *m_current_used_step_index > 0;
}

bool TraversableSessionHistory::can_go_forward() const
{
    return m_current_used_step_index.has_value() && *m_current_used_step_index + 1 < m_used_steps.size();
}

bool TraversableSessionHistory::has_only_top_level_used_steps() const
{
    if (entries_have_nested_histories(m_entries))
        return false;

    if (m_entries.size() != m_used_steps.size())
        return false;

    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i]->step != m_used_steps[i])
            return false;
    }
    return true;
}

Optional<TraversableSessionHistory::TraversalTarget> TraversableSessionHistory::traversal_target_for_delta(int delta) const
{
    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#traverse-the-history-by-a-delta

    // 1. Let allSteps be the result of getting all used history steps for traversable.
    // NB: m_used_steps is the cached result for the canonical traversable session history.

    // 2. Let currentStepIndex be the index of traversable's current session history step within allSteps.

    // 3. Let targetStepIndex be currentStepIndex plus delta.
    auto target_step_index = target_step_index_for_delta(delta);

    // 4. If allSteps[targetStepIndex] does not exist, then abort these steps.
    if (!target_step_index.has_value())
        return {};

    auto target_step = step_at(*target_step_index);
    VERIFY(target_step.has_value());
    return traversal_target_for_step(*target_step);
}

Optional<TraversableSessionHistory::TraversalTarget> TraversableSessionHistory::traversal_target_for_step(i32 step) const
{
    auto target_step_index = m_used_steps.find_first_index(step);
    if (!target_step_index.has_value())
        return {};

    auto target_top_level_entry_index = top_level_entry_index_for_step(m_entries, step);
    VERIFY(target_top_level_entry_index.has_value());
    auto const* target_top_level_entry = m_entries[*target_top_level_entry_index].ptr();
    auto const* current_top_level_entry = current_entry();
    VERIFY(current_top_level_entry);

    return TraversalTarget {
        .target_step_index = *target_step_index,
        .target_step = step,
        .target_top_level_entry_index = *target_top_level_entry_index,
        .target_top_level_entry = target_top_level_entry,
        .target_step_is_top_level_entry = entry_for_step(step) != nullptr,
        .changes_top_level_entry = target_top_level_entry != current_top_level_entry,
    };
}

Optional<Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>>&> TraversableSessionHistory::get_session_history_entries(CanonicalNavigable const& navigable)
{
    auto entries = const_cast<TraversableSessionHistory const&>(*this).get_session_history_entries(navigable);
    if (!entries.has_value())
        return {};
    return const_cast<Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>>&>(*entries);
}

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#getting-session-history-entries
Optional<Vector<NonnullRefPtr<CanonicalSessionHistoryEntry>> const&> TraversableSessionHistory::get_session_history_entries(CanonicalNavigable const& navigable) const
{
    // 1. Let traversable be navigable's traversable navigable.
    // NB: The caller has already resolved navigable through its CanonicalTraversable.

    // FIXME: 2. Assert: this is running within traversable's session history traversal queue.

    // 3. If navigable is traversable, return traversable's session history entries.
    if (navigable.is_top_level_traversable())
        return m_entries;

    // 4. Let docStates be an empty ordered set of document states.
    Vector<CanonicalDocumentState const*> document_states;
    auto append_document_state = [&](CanonicalDocumentState const& document_state) {
        if (!document_states.contains_slow(&document_state))
            document_states.append(&document_state);
    };

    // 5. For each entry of traversable's session history entries, append entry's document state to docStates.
    for (auto const& entry : m_entries)
        append_document_state(*entry->document_state);

    // 6. For each docState of docStates:
    for (size_t i = 0; i < document_states.size(); ++i) {
        auto const& document_state = *document_states[i];

        // 1. For each nestedHistory of docState's nested histories:
        for (auto const& nested_history : document_state.nested_histories) {
            // 1. If nestedHistory's id equals navigable's id, return nestedHistory's entries.
            if (nested_history.id == navigable.id())
                return nested_history.entries;

            // 2. For each entry of nestedHistory's entries, append entry's document state to docStates.
            for (auto const& entry : nested_history.entries)
                append_document_state(*entry->document_state);
        }
    }

    // FIXME: The UI mirror can temporarily lack a newly-created navigable's nested history while WebContent and the
    //        UI process converge. Once navigable creation is ordered with session history updates, apply the
    //        specification's final assertion.
    return {};
}

Optional<size_t> TraversableSessionHistory::target_step_index_for_delta(int delta) const
{
    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#traverse-the-history-by-a-delta
    // Let allSteps be the result of getting all used history steps. Let
    // targetStepIndex be currentStepIndex plus delta. If allSteps[targetStepIndex]
    // does not exist, then abort these steps.
    if (!m_current_used_step_index.has_value() || delta == 0)
        return {};

    if (delta < 0) {
        auto magnitude = static_cast<size_t>(-static_cast<i64>(delta));
        if (magnitude > *m_current_used_step_index)
            return {};
        return *m_current_used_step_index - magnitude;
    }

    auto target_index = *m_current_used_step_index + static_cast<size_t>(delta);
    if (target_index >= m_used_steps.size())
        return {};
    return target_index;
}

Optional<i32> TraversableSessionHistory::step_at(size_t index) const
{
    if (index >= m_used_steps.size())
        return {};
    return m_used_steps[index];
}

CanonicalSessionHistoryEntry* TraversableSessionHistory::current_entry() const
{
    if (!m_current_used_step_index.has_value())
        return nullptr;
    return top_level_entry_for_step(m_used_steps[*m_current_used_step_index]);
}

CanonicalSessionHistoryEntry* TraversableSessionHistory::entry_at(size_t index) const
{
    if (index >= m_entries.size())
        return nullptr;
    return m_entries[index].ptr();
}

CanonicalSessionHistoryEntry* TraversableSessionHistory::entry_for_step(i32 step) const
{
    for (auto const& entry : m_entries) {
        if (entry->step == step)
            return entry.ptr();
    }
    return nullptr;
}

CanonicalSessionHistoryEntry* TraversableSessionHistory::top_level_entry_for_step(i32 step) const
{
    auto index = top_level_entry_index_for_step(m_entries, step);
    if (!index.has_value())
        return nullptr;
    return m_entries[*index].ptr();
}

void TraversableSessionHistory::traverse_to(size_t index)
{
    VERIFY(index < m_used_steps.size());
    m_current_used_step_index = index;
}

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#getting-the-used-step
Optional<i32> TraversableSessionHistory::get_the_used_step(i32 step) const
{
    // 1. Let steps be the result of getting all used history steps within traversable.
    // 2. Return the greatest item in steps that is less than or equal to step.
    Optional<i32> used_step;
    for (auto candidate : m_used_steps) {
        if (candidate <= step && (!used_step.has_value() || candidate > *used_step))
            used_step = candidate;
    }
    return used_step;
}

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#getting-the-target-history-entry
CanonicalSessionHistoryEntry* TraversableSessionHistory::get_the_target_history_entry(CanonicalNavigable const& navigable, i32 step) const
{
    // 1. Let entries be the result of getting session history entries for navigable.
    auto entries = get_session_history_entries(navigable);
    if (!entries.has_value())
        return nullptr;

    // 2. Return the item in entries that has the greatest step less than or equal to step.
    CanonicalSessionHistoryEntry* target_entry = nullptr;
    for (auto const& entry : *entries) {
        if (entry->step <= step && (!target_entry || entry->step > target_entry->step))
            target_entry = entry.ptr();
    }
    return target_entry;
}

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#getting-the-history-object-length-and-index
Optional<Web::HTML::HistoryObjectLengthAndIndex> TraversableSessionHistory::get_the_history_object_length_and_index(i32 step) const
{
    // 1. Let steps be the result of getting all used history steps within traversable.
    // 2. Let scriptHistoryLength be the size of steps.
    auto script_history_length = m_used_steps.size();

    // 3. Assert: steps contains step.
    // AD-HOC: The canonical mirror can be reconciling a removed child navigable, so answer nothing instead of
    //         asserting; the caller treats it as a failed job.
    auto script_history_index = m_used_steps.find_first_index(step);
    if (!script_history_index.has_value())
        return {};

    // 4. Let scriptHistoryIndex be the index of step in steps.
    // 5. Return (scriptHistoryLength, scriptHistoryIndex).
    return Web::HTML::HistoryObjectLengthAndIndex {
        .script_history_length = script_history_length,
        .script_history_index = *script_history_index,
    };
}

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#getting-session-history-entries-for-the-navigation-api
Optional<Vector<Web::HTML::SessionHistoryEntryDescriptor>> TraversableSessionHistory::get_session_history_entries_for_the_navigation_api(CanonicalNavigable const& navigable, i32 target_step) const
{
    // 1. Let rawEntries be the result of getting session history entries for navigable.
    auto raw_entries = get_session_history_entries(navigable);
    if (!raw_entries.has_value())
        return {};

    // 2. Let entriesForNavigationAPI be a new empty list.
    Vector<Web::HTML::SessionHistoryEntryDescriptor> entries_for_navigation_api;

    // 3. Let startingIndex be the index of the session history entry in rawEntries who has the greatest step less
    //    than or equal to targetStep.
    Optional<size_t> starting_index;
    Optional<i32> greatest_step;
    for (size_t i = 0; i < raw_entries->size(); ++i) {
        auto const& entry = raw_entries->at(i);
        if (entry->step <= target_step && (!greatest_step.has_value() || entry->step > *greatest_step)) {
            starting_index = i;
            greatest_step = entry->step;
        }
    }
    if (!starting_index.has_value())
        return entries_for_navigation_api;

    // 4. Append rawEntries[startingIndex] to entriesForNavigationAPI.
    entries_for_navigation_api.append(raw_entries->at(*starting_index)->descriptor());

    // 5. Let startingOrigin be rawEntries[startingIndex]'s document state's origin.
    auto const& starting_entry = *raw_entries->at(*starting_index);
    auto const& starting_origin = starting_entry.document_state->origin;

    // 6. Let i be startingIndex − 1.
    auto i = static_cast<i64>(*starting_index) - 1;

    // 7. While i > 0:
    // AD-HOC: Implement "while i >= 0" to avoid dropping a same-origin rawEntries[0].
    //         https://github.com/whatwg/html/issues/12644
    while (i >= 0) {
        auto const& entry = *raw_entries->at(static_cast<size_t>(i));

        // 1. If rawEntries[i]'s document state's origin is not same origin with startingOrigin, then break.
        auto const& entry_origin = entry.document_state->origin;
        if (entry.document_state != starting_entry.document_state
            && (!starting_origin.has_value() || !entry_origin.has_value()
                || !entry_origin->is_same_origin(*starting_origin))) {
            break;
        }

        // 2. Prepend rawEntries[i] to entriesForNavigationAPI.
        entries_for_navigation_api.prepend(entry.descriptor());

        // 3. Set i to i − 1.
        --i;
    }

    // 8. Set i to startingIndex + 1.
    i = static_cast<i64>(*starting_index) + 1;

    // 9. While i < rawEntries's size:
    while (i < static_cast<i64>(raw_entries->size())) {
        auto const& entry = *raw_entries->at(static_cast<size_t>(i));

        // 1. If rawEntries[i]'s document state's origin is not same origin with startingOrigin, then break.
        auto const& entry_origin = entry.document_state->origin;
        if (entry.document_state != starting_entry.document_state
            && (!starting_origin.has_value() || !entry_origin.has_value()
                || !entry_origin->is_same_origin(*starting_origin))) {
            break;
        }

        // 2. Append rawEntries[i] to entriesForNavigationAPI.
        entries_for_navigation_api.append(entry.descriptor());

        // 3. Set i to i + 1.
        ++i;
    }

    // 10. Return entriesForNavigationAPI.
    return entries_for_navigation_api;
}

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#get-all-navigables-whose-current-session-history-entry-will-change-or-reload
Vector<Web::HTML::CrossProcessId> TraversableSessionHistory::get_all_navigables_whose_current_session_history_entry_will_change_or_reload(CanonicalNavigable const& traversable, i32 target_step) const
{
    // 1. Let results be an empty list.
    Vector<Web::HTML::CrossProcessId> results;

    // 2. Let navigablesToCheck be « traversable ».
    Vector<CanonicalNavigable const*> navigables_to_check { &traversable };

    // 3. For each navigable of navigablesToCheck:
    while (!navigables_to_check.is_empty()) {
        auto const* navigable = navigables_to_check.take_first();

        // 1. Let targetEntry be the result of getting the target history entry given navigable and targetStep.
        auto const* target_entry = get_the_target_history_entry(*navigable, target_step);
        if (!target_entry)
            continue;

        // 2. If targetEntry is not navigable's current session history entry or targetEntry's document state's reload
        //    pending is true, then append navigable to results.
        if (!navigable->current_session_history_entry_is(*target_entry) || target_entry->document_state->reload_pending)
            results.append(navigable->id());

        // 3. If targetEntry's document is navigable's document, and targetEntry's document state's reload pending is
        //    false, then extend navigablesToCheck with the child navigables of navigable.
        if (navigable->active_document_is(*target_entry) && !target_entry->document_state->reload_pending) {
            for (auto const& child : navigable->children())
                navigables_to_check.append(child.ptr());
        }
    }

    // 4. Return results.
    return results;
}

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#getting-all-navigables-that-might-experience-a-cross-document-traversal
Vector<Web::HTML::CrossProcessId> TraversableSessionHistory::get_all_navigables_that_might_experience_a_cross_document_traversal(CanonicalNavigable const& traversable, i32 target_step) const
{
    // 1. Let results be an empty list.
    Vector<Web::HTML::CrossProcessId> results;

    // 2. Let navigablesToCheck be « traversable ».
    Vector<CanonicalNavigable const*> navigables_to_check { &traversable };

    // 3. For each navigable of navigablesToCheck:
    while (!navigables_to_check.is_empty()) {
        auto const* navigable = navigables_to_check.take_first();

        // 1. Let targetEntry be the result of getting the target history entry given navigable and targetStep.
        auto const* target_entry = get_the_target_history_entry(*navigable, target_step);
        if (!target_entry)
            continue;

        // 2. If targetEntry's document is not navigable's document or targetEntry's document state's reload pending
        //    is true, then append navigable to results.
        if (!navigable->active_document_is(*target_entry) || target_entry->document_state->reload_pending) {
            results.append(navigable->id());
        }

        // 3. Otherwise, extend navigablesToCheck with navigable's child navigables.
        else {
            for (auto const& child : navigable->children())
                navigables_to_check.append(child.ptr());
        }
    }

    // 4. Return results.
    return results;
}

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#getting-all-navigables-that-only-need-history-object-length/index-update
Vector<Web::HTML::CrossProcessId> TraversableSessionHistory::get_all_navigables_that_only_need_history_object_length_index_update(CanonicalNavigable const& traversable, i32 target_step) const
{
    // 1. Let results be an empty list.
    Vector<Web::HTML::CrossProcessId> results;

    // 2. Let navigablesToCheck be « traversable ».
    Vector<CanonicalNavigable const*> navigables_to_check { &traversable };

    // 3. For each navigable of navigablesToCheck:
    while (!navigables_to_check.is_empty()) {
        auto const* navigable = navigables_to_check.take_first();

        // 1. Let targetEntry be the result of getting the target history entry given navigable and targetStep.
        auto const* target_entry = get_the_target_history_entry(*navigable, target_step);
        if (!target_entry)
            continue;

        // 2. If targetEntry is navigable's current session history entry and targetEntry's document state's reload
        //    pending is false:
        if (navigable->current_session_history_entry_is(*target_entry) && !target_entry->document_state->reload_pending) {
            // 1. Append navigable to results.
            results.append(navigable->id());

            // 2. Extend navigablesToCheck with navigable's child navigables.
            for (auto const& child : navigable->children())
                navigables_to_check.append(child.ptr());
        }
    }

    // 4. Return results.
    return results;
}

}
