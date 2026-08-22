/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibIPC/Decoder.h>
#include <LibIPC/Encoder.h>
#include <LibWeb/HTML/HistoryOperation.h>

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::FinalizeCrossDocumentNavigationRequest const& parameters)
{
    TRY(encoder.encode(parameters.navigable_id));
    TRY(encoder.encode(parameters.history_entry));
    TRY(encoder.encode(parameters.navigation_id));
    TRY(encoder.encode(parameters.history_handling));
    TRY(encoder.encode(parameters.user_involvement));
    return {};
}

template<>
ErrorOr<Web::FinalizeCrossDocumentNavigationRequest> IPC::decode(Decoder& decoder)
{
    return Web::FinalizeCrossDocumentNavigationRequest {
        .navigable_id = TRY(decoder.decode<Web::HTML::CrossProcessId>()),
        .history_entry = TRY(decoder.decode<Web::HTML::PendingSessionHistoryEntryDescriptor>()),
        .navigation_id = TRY(decoder.decode<Optional<Utf16String>>()),
        .history_handling = TRY(decoder.decode<Web::HTML::HistoryHandlingBehavior>()),
        .user_involvement = TRY(decoder.decode<Web::HTML::UserNavigationInvolvement>()),
    };
}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::CrossDocumentNavigationFinalizationHostState const& state)
{
    TRY(encoder.encode(state.pending_document_is_in_auxiliary_browsing_context_with_opener));
    TRY(encoder.encode(state.pending_document_origin));
    TRY(encoder.encode(state.active_document_origin));
    return {};
}

template<>
ErrorOr<Web::CrossDocumentNavigationFinalizationHostState> IPC::decode(Decoder& decoder)
{
    return Web::CrossDocumentNavigationFinalizationHostState {
        .pending_document_is_in_auxiliary_browsing_context_with_opener = TRY(decoder.decode<bool>()),
        .pending_document_origin = TRY(decoder.decode<Optional<URL::Origin>>()),
        .active_document_origin = TRY(decoder.decode<Optional<URL::Origin>>()),
    };
}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::ReconstructedChildNavigation const& navigation)
{
    TRY(encoder.encode(navigation.target_entry));
    TRY(encoder.encode(navigation.navigation_id));
    return {};
}

template<>
ErrorOr<Web::ReconstructedChildNavigation> IPC::decode(Decoder& decoder)
{
    return Web::ReconstructedChildNavigation {
        .target_entry = TRY(decoder.decode<Web::HTML::SessionHistoryEntryDescriptor>()),
        .navigation_id = TRY(decoder.decode<Utf16String>()),
    };
}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::ReloadRequest const& parameters)
{
    TRY(encoder.encode(parameters.navigable_id));
    TRY(encoder.encode(parameters.user_involvement));
    return {};
}

template<>
ErrorOr<Web::ReloadRequest> IPC::decode(Decoder& decoder)
{
    return Web::ReloadRequest {
        .navigable_id = TRY(decoder.decode<Web::HTML::CrossProcessId>()),
        .user_involvement = TRY(decoder.decode<Web::HTML::UserNavigationInvolvement>()),
    };
}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::TraverseHistoryByADeltaRequest const& parameters)
{
    TRY(encoder.encode(parameters.traversable_id));
    TRY(encoder.encode(parameters.delta));
    TRY(encoder.encode(parameters.initiator_to_check));
    TRY(encoder.encode(parameters.source_snapshot_params));
    TRY(encoder.encode(parameters.user_involvement));
    return {};
}

template<>
ErrorOr<Web::TraverseHistoryByADeltaRequest> IPC::decode(Decoder& decoder)
{
    return Web::TraverseHistoryByADeltaRequest {
        .traversable_id = TRY(decoder.decode<Web::HTML::CrossProcessId>()),
        .delta = TRY(decoder.decode<i32>()),
        .initiator_to_check = TRY(decoder.decode<Optional<Web::HTML::CrossProcessId>>()),
        .source_snapshot_params = TRY(decoder.decode<Optional<Web::SerializedSourceSnapshotParams>>()),
        .user_involvement = TRY(decoder.decode<Web::HTML::UserNavigationInvolvement>()),
    };
}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::SerializedSourceSnapshotParams const& snapshot)
{
    TRY(encoder.encode(snapshot.sandboxing_flags));
    TRY(encoder.encode(snapshot.has_transient_activation));
    return {};
}

template<>
ErrorOr<Web::SerializedSourceSnapshotParams> IPC::decode(Decoder& decoder)
{
    return Web::SerializedSourceSnapshotParams {
        .sandboxing_flags = TRY(decoder.decode<Web::HTML::SandboxingFlagSet>()),
        .has_transient_activation = TRY(decoder.decode<bool>()),
    };
}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::BrowserHistoryTraversalRequest const& parameters)
{
    TRY(encoder.encode(parameters.traversable_id));
    TRY(encoder.encode(parameters.target_step));
    TRY(encoder.encode(parameters.user_involvement));
    return {};
}

template<>
ErrorOr<Web::BrowserHistoryTraversalRequest> IPC::decode(Decoder& decoder)
{
    return Web::BrowserHistoryTraversalRequest {
        .traversable_id = TRY(decoder.decode<Web::HTML::CrossProcessId>()),
        .target_step = TRY(decoder.decode<i32>()),
        .user_involvement = TRY(decoder.decode<Web::HTML::UserNavigationInvolvement>()),
    };
}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::PerformNavigationAPITraversalRequest const& parameters)
{
    TRY(encoder.encode(parameters.navigable_id));
    TRY(encoder.encode(parameters.key));
    TRY(encoder.encode(parameters.source_snapshot_params));
    return {};
}

template<>
ErrorOr<Web::PerformNavigationAPITraversalRequest> IPC::decode(Decoder& decoder)
{
    return Web::PerformNavigationAPITraversalRequest {
        .navigable_id = TRY(decoder.decode<Web::HTML::CrossProcessId>()),
        .key = TRY(decoder.decode<Utf16String>()),
        .source_snapshot_params = TRY(decoder.decode<Web::SerializedSourceSnapshotParams>()),
    };
}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::ResumeTraverseHistoryStepRequest const& parameters)
{
    TRY(encoder.encode(parameters.navigable_id));
    TRY(encoder.encode(parameters.target_step));
    TRY(encoder.encode(parameters.user_involvement));
    return {};
}

template<>
ErrorOr<Web::ResumeTraverseHistoryStepRequest> IPC::decode(Decoder& decoder)
{
    return Web::ResumeTraverseHistoryStepRequest {
        .navigable_id = TRY(decoder.decode<Web::HTML::CrossProcessId>()),
        .target_step = TRY(decoder.decode<i32>()),
        .user_involvement = TRY(decoder.decode<Web::HTML::UserNavigationInvolvement>()),
    };
}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::CreateNewChildNavigableRequest const& parameters)
{
    TRY(encoder.encode(parameters.parent_navigable_id));
    TRY(encoder.encode(parameters.navigable_id));
    TRY(encoder.encode(parameters.initial_history_entry));
    return {};
}

template<>
ErrorOr<Web::CreateNewChildNavigableRequest> IPC::decode(Decoder& decoder)
{
    return Web::CreateNewChildNavigableRequest {
        .parent_navigable_id = TRY(decoder.decode<Web::HTML::CrossProcessId>()),
        .navigable_id = TRY(decoder.decode<Web::HTML::CrossProcessId>()),
        .initial_history_entry = TRY(decoder.decode<Web::HTML::PendingSessionHistoryEntryDescriptor>()),
    };
}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::DestroyChildNavigableRequest const& parameters)
{
    TRY(encoder.encode(parameters.parent_navigable_id));
    TRY(encoder.encode(parameters.parent_document_state_id));
    TRY(encoder.encode(parameters.navigable_id));
    return {};
}

template<>
ErrorOr<Web::DestroyChildNavigableRequest> IPC::decode(Decoder& decoder)
{
    return Web::DestroyChildNavigableRequest {
        .parent_navigable_id = TRY(decoder.decode<Web::HTML::CrossProcessId>()),
        .parent_document_state_id = TRY(decoder.decode<Web::HTML::CrossProcessId>()),
        .navigable_id = TRY(decoder.decode<Web::HTML::CrossProcessId>()),
    };
}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::FinalizeSameDocumentNavigationRequest const& parameters)
{
    TRY(encoder.encode(parameters.navigable_id));
    TRY(encoder.encode(parameters.target_entry));
    TRY(encoder.encode(parameters.entry_to_replace));
    TRY(encoder.encode(parameters.previous_entry_persisted_state));
    TRY(encoder.encode(parameters.history_handling));
    TRY(encoder.encode(parameters.user_involvement));
    return {};
}

template<>
ErrorOr<Web::FinalizeSameDocumentNavigationRequest> IPC::decode(Decoder& decoder)
{
    return Web::FinalizeSameDocumentNavigationRequest {
        .navigable_id = TRY(decoder.decode<Web::HTML::CrossProcessId>()),
        .target_entry = TRY(decoder.decode<Web::HTML::SameDocumentSessionHistoryEntryDescriptor>()),
        .entry_to_replace = TRY(decoder.decode<Optional<Web::HTML::SessionHistoryEntryIdentity>>()),
        .previous_entry_persisted_state = TRY(decoder.decode<Optional<Web::HTML::SessionHistoryEntryPersistedState>>()),
        .history_handling = TRY(decoder.decode<Web::HTML::HistoryHandlingBehavior>()),
        .user_involvement = TRY(decoder.decode<Web::HTML::UserNavigationInvolvement>()),
    };
}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::DefinitelyCloseTopLevelTraversableRequest const& parameters)
{
    return encoder.encode(parameters.traversable_id);
}

template<>
ErrorOr<Web::DefinitelyCloseTopLevelTraversableRequest> IPC::decode(Decoder& decoder)
{
    return Web::DefinitelyCloseTopLevelTraversableRequest {
        .traversable_id = TRY(decoder.decode<Web::HTML::CrossProcessId>()),
    };
}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::FlushSessionHistoryTraversalQueueForTestingRequest const& parameters)
{
    return encoder.encode(parameters.traversable_id);
}

template<>
ErrorOr<Web::FlushSessionHistoryTraversalQueueForTestingRequest> IPC::decode(Decoder& decoder)
{
    return Web::FlushSessionHistoryTraversalQueueForTestingRequest {
        .traversable_id = TRY(decoder.decode<Web::HTML::CrossProcessId>()),
    };
}
