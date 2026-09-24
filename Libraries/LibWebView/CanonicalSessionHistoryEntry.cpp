/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/CanonicalSessionHistoryEntry.h>

namespace WebView {

NonnullRefPtr<CanonicalDocumentState> CanonicalDocumentState::create(Web::HTML::CrossProcessId id)
{
    return adopt_ref(*new CanonicalDocumentState(id));
}

NonnullRefPtr<CanonicalDocumentState> CanonicalDocumentState::create(Web::HTML::CrossProcessId id, NonnullRefPtr<CanonicalDocument> document)
{
    auto document_state = create(id);
    document_state->document = move(document);
    return document_state;
}

CanonicalDocumentState::CanonicalDocumentState(Web::HTML::CrossProcessId id)
    : id(id)
{
}

CanonicalDocumentState::~CanonicalDocumentState() = default;

Web::HTML::SessionHistoryDocumentStateDescriptor CanonicalDocumentState::descriptor() const
{
    Vector<Web::HTML::SessionHistoryNestedHistoryDescriptor> nested_history_descriptors;
    nested_history_descriptors.ensure_capacity(nested_histories.size());
    for (auto const& nested_history : nested_histories) {
        Vector<Web::HTML::SessionHistoryEntryDescriptor> entries;
        entries.ensure_capacity(nested_history.entries.size());
        for (auto const& entry : nested_history.entries)
            entries.unchecked_append(entry->descriptor());
        nested_history_descriptors.unchecked_append({ .id = nested_history.id, .entries = move(entries) });
    }

    return {
        .id = id,
        .history_policy_container = history_policy_container,
        .request_referrer = request_referrer,
        .request_referrer_policy = request_referrer_policy,
        .initiator_origin = initiator_origin,
        .origin = origin,
        .about_base_url = about_base_url,
        .resource = resource,
        .reload_pending = reload_pending,
        .ever_populated = ever_populated,
        .navigable_target_name = navigable_target_name,
        .nested_histories = move(nested_history_descriptors),
    };
}

void CanonicalDocumentState::copy_values_from(CanonicalDocumentState const& other)
{
    history_policy_container = other.history_policy_container;
    request_referrer = other.request_referrer;
    request_referrer_policy = other.request_referrer_policy;
    initiator_origin = other.initiator_origin;
    origin = other.origin;
    about_base_url = other.about_base_url;
    resource = other.resource;
    reload_pending = other.reload_pending;
    ever_populated = other.ever_populated;
    navigable_target_name = other.navigable_target_name;
    nested_histories = other.nested_histories;
}

static NonnullRefPtr<CanonicalSessionHistoryEntry> create_entry_from_descriptor(Web::HTML::SessionHistoryEntryDescriptor const&, CanonicalSessionHistoryEntry::DocumentStates&, CanonicalSessionHistoryEntry::UpdateDocumentState, Vector<Web::HTML::CrossProcessId>& ancestor_ids);

static NonnullRefPtr<CanonicalDocumentState> document_state_from_descriptor(Web::HTML::SessionHistoryDocumentStateDescriptor const& descriptor, CanonicalSessionHistoryEntry::DocumentStates& document_states, CanonicalSessionHistoryEntry::UpdateDocumentState update_document_state, Vector<Web::HTML::CrossProcessId>& ancestor_ids)
{
    // A document state cannot be among its own nested histories' entries. A descriptor naming an ancestor's document
    // state names another one.
    auto names_ancestor = ancestor_ids.contains_slow(descriptor.id);
    RefPtr<CanonicalDocumentState> existing = names_ancestor ? nullptr : document_states.get(descriptor.id).value_or(nullptr);
    if (existing && update_document_state == CanonicalSessionHistoryEntry::UpdateDocumentState::No)
        return existing.release_nonnull();

    auto document_state = existing ? existing.release_nonnull() : CanonicalDocumentState::create(descriptor.id);
    if (!names_ancestor)
        document_states.set(descriptor.id, document_state);
    document_state->history_policy_container = descriptor.history_policy_container;
    document_state->request_referrer = descriptor.request_referrer;
    document_state->request_referrer_policy = descriptor.request_referrer_policy;
    document_state->initiator_origin = descriptor.initiator_origin;
    document_state->origin = descriptor.origin;
    document_state->about_base_url = descriptor.about_base_url;
    document_state->resource = descriptor.resource;
    document_state->reload_pending = descriptor.reload_pending;
    document_state->ever_populated = descriptor.ever_populated;
    document_state->navigable_target_name = descriptor.navigable_target_name;
    document_state->nested_histories.clear();
    ancestor_ids.append(descriptor.id);
    for (auto const& nested_history : descriptor.nested_histories) {
        CanonicalNestedHistory canonical_nested_history { .id = nested_history.id, .entries = {} };
        for (auto const& entry : nested_history.entries)
            canonical_nested_history.entries.append(create_entry_from_descriptor(entry, document_states, update_document_state, ancestor_ids));
        document_state->nested_histories.append(move(canonical_nested_history));
    }
    ancestor_ids.take_last();
    return document_state;
}

NonnullRefPtr<CanonicalSessionHistoryEntry> CanonicalSessionHistoryEntry::create_from_descriptor(Web::HTML::SessionHistoryEntryDescriptor const& descriptor)
{
    DocumentStates document_states;
    return create_from_descriptor(descriptor, document_states);
}

NonnullRefPtr<CanonicalSessionHistoryEntry> CanonicalSessionHistoryEntry::create_from_descriptor(Web::HTML::SessionHistoryEntryDescriptor const& descriptor, DocumentStates& document_states, UpdateDocumentState update_document_state)
{
    Vector<Web::HTML::CrossProcessId> ancestor_ids;
    return create_entry_from_descriptor(descriptor, document_states, update_document_state, ancestor_ids);
}

static NonnullRefPtr<CanonicalSessionHistoryEntry> create_entry_from_descriptor(Web::HTML::SessionHistoryEntryDescriptor const& descriptor, CanonicalSessionHistoryEntry::DocumentStates& document_states, CanonicalSessionHistoryEntry::UpdateDocumentState update_document_state, Vector<Web::HTML::CrossProcessId>& ancestor_ids)
{
    auto entry = CanonicalSessionHistoryEntry::create(document_state_from_descriptor(descriptor.document_state, document_states, update_document_state, ancestor_ids));
    entry->step = descriptor.step;
    entry->url = descriptor.url;
    entry->classic_history_api_state = descriptor.classic_history_api_state;
    entry->navigation_api_state = descriptor.navigation_api_state;
    entry->navigation_api_key = descriptor.navigation_api_key;
    entry->navigation_api_id = descriptor.navigation_api_id;
    entry->scroll_restoration_mode = descriptor.scroll_restoration_mode;
    entry->scroll_position_data = descriptor.scroll_position_data;
    return entry;
}

NonnullRefPtr<CanonicalSessionHistoryEntry> CanonicalSessionHistoryEntry::create(NonnullRefPtr<CanonicalDocumentState> document_state)
{
    return adopt_ref(*new CanonicalSessionHistoryEntry(move(document_state)));
}

CanonicalSessionHistoryEntry::CanonicalSessionHistoryEntry(NonnullRefPtr<CanonicalDocumentState> document_state)
    : document_state(move(document_state))
{
}

CanonicalSessionHistoryEntry::~CanonicalSessionHistoryEntry() = default;

Web::HTML::SessionHistoryEntryDescriptor CanonicalSessionHistoryEntry::descriptor() const
{
    return {
        .step = step,
        .url = url,
        .document_state = document_state->descriptor(),
        .classic_history_api_state = classic_history_api_state,
        .navigation_api_state = navigation_api_state,
        .navigation_api_key = navigation_api_key,
        .navigation_api_id = navigation_api_id,
        .scroll_restoration_mode = scroll_restoration_mode,
        .scroll_position_data = scroll_position_data,
    };
}

}
