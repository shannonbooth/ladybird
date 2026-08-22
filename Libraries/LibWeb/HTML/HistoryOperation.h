/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Utf16String.h>
#include <AK/Variant.h>
#include <LibIPC/Forward.h>
#include <LibWeb/Export.h>
#include <LibWeb/HTML/ApplyHistoryStep.h>
#include <LibWeb/HTML/CrossProcessId.h>
#include <LibWeb/HTML/HistoryHandlingBehavior.h>
#include <LibWeb/HTML/SameDocumentSessionHistoryEntryDescriptor.h>
#include <LibWeb/HTML/SandboxingFlagSet.h>
#include <LibWeb/HTML/SessionHistoryEntry.h>
#include <LibWeb/HTML/UserNavigationInvolvement.h>

namespace Web {

struct FinalizeCrossDocumentNavigationRequest {
    HTML::CrossProcessId navigable_id;
    HTML::PendingSessionHistoryEntryDescriptor history_entry;
    Optional<Utf16String> navigation_id;
    HTML::HistoryHandlingBehavior history_handling;
    HTML::UserNavigationInvolvement user_involvement;
};

struct CrossDocumentNavigationFinalizationHostState {
    bool pending_document_is_in_auxiliary_browsing_context_with_opener { false };
    Optional<URL::Origin> pending_document_origin;
    Optional<URL::Origin> active_document_origin;
};

struct ReconstructedChildNavigation {
    HTML::SessionHistoryEntryDescriptor target_entry;
    Utf16String navigation_id;
};

using HistoryOperationReadyResult = Variant<
    Empty,
    HTML::HistoryStepResult,
    HTML::CrossProcessId,
    CrossDocumentNavigationFinalizationHostState>;

struct ReloadRequest {
    HTML::CrossProcessId navigable_id;
    HTML::UserNavigationInvolvement user_involvement;
};

struct SerializedSourceSnapshotParams {
    HTML::SandboxingFlagSet sandboxing_flags {};
    bool has_transient_activation { false };
};

struct TraverseHistoryByADeltaRequest {
    HTML::CrossProcessId traversable_id;
    i32 delta;
    Optional<HTML::CrossProcessId> initiator_to_check;
    Optional<SerializedSourceSnapshotParams> source_snapshot_params;
    HTML::UserNavigationInvolvement user_involvement;
};

struct BrowserHistoryTraversalRequest {
    HTML::CrossProcessId traversable_id;
    i32 target_step;
    HTML::UserNavigationInvolvement user_involvement;
};

struct PerformNavigationAPITraversalRequest {
    HTML::CrossProcessId navigable_id;
    Utf16String key;
    SerializedSourceSnapshotParams source_snapshot_params;
};

struct ResumeTraverseHistoryStepRequest {
    HTML::CrossProcessId navigable_id;
    i32 target_step;
    HTML::UserNavigationInvolvement user_involvement;
};

struct CreateNewChildNavigableRequest {
    HTML::CrossProcessId parent_navigable_id;
    HTML::CrossProcessId navigable_id;
    HTML::PendingSessionHistoryEntryDescriptor initial_history_entry;
};

struct DestroyChildNavigableRequest {
    HTML::CrossProcessId parent_navigable_id;
    HTML::CrossProcessId parent_document_state_id;
    HTML::CrossProcessId navigable_id;
};

struct FinalizeSameDocumentNavigationRequest {
    HTML::CrossProcessId navigable_id;
    HTML::SameDocumentSessionHistoryEntryDescriptor target_entry;
    Optional<HTML::SessionHistoryEntryIdentity> entry_to_replace;
    Optional<HTML::SessionHistoryEntryPersistedState> previous_entry_persisted_state;
    HTML::HistoryHandlingBehavior history_handling;
    HTML::UserNavigationInvolvement user_involvement;
};

struct DefinitelyCloseTopLevelTraversableRequest {
    HTML::CrossProcessId traversable_id;
};

struct FlushSessionHistoryTraversalQueueForTestingRequest {
    HTML::CrossProcessId traversable_id;
};

using HistoryOperationRequest = Variant<
    FinalizeCrossDocumentNavigationRequest,
    ReloadRequest,
    TraverseHistoryByADeltaRequest,
    BrowserHistoryTraversalRequest,
    PerformNavigationAPITraversalRequest,
    ResumeTraverseHistoryStepRequest,
    CreateNewChildNavigableRequest,
    DestroyChildNavigableRequest,
    FinalizeSameDocumentNavigationRequest,
    DefinitelyCloseTopLevelTraversableRequest,
    FlushSessionHistoryTraversalQueueForTestingRequest>;

}

namespace IPC {

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::FinalizeCrossDocumentNavigationRequest const&);
template<>
WEB_API ErrorOr<Web::FinalizeCrossDocumentNavigationRequest> decode(Decoder&);

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::CrossDocumentNavigationFinalizationHostState const&);
template<>
WEB_API ErrorOr<Web::CrossDocumentNavigationFinalizationHostState> decode(Decoder&);

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::ReconstructedChildNavigation const&);
template<>
WEB_API ErrorOr<Web::ReconstructedChildNavigation> decode(Decoder&);

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::ReloadRequest const&);
template<>
WEB_API ErrorOr<Web::ReloadRequest> decode(Decoder&);

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::SerializedSourceSnapshotParams const&);
template<>
WEB_API ErrorOr<Web::SerializedSourceSnapshotParams> decode(Decoder&);

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::TraverseHistoryByADeltaRequest const&);
template<>
WEB_API ErrorOr<Web::TraverseHistoryByADeltaRequest> decode(Decoder&);

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::BrowserHistoryTraversalRequest const&);
template<>
WEB_API ErrorOr<Web::BrowserHistoryTraversalRequest> decode(Decoder&);

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::PerformNavigationAPITraversalRequest const&);
template<>
WEB_API ErrorOr<Web::PerformNavigationAPITraversalRequest> decode(Decoder&);

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::ResumeTraverseHistoryStepRequest const&);
template<>
WEB_API ErrorOr<Web::ResumeTraverseHistoryStepRequest> decode(Decoder&);

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::CreateNewChildNavigableRequest const&);
template<>
WEB_API ErrorOr<Web::CreateNewChildNavigableRequest> decode(Decoder&);

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::DestroyChildNavigableRequest const&);
template<>
WEB_API ErrorOr<Web::DestroyChildNavigableRequest> decode(Decoder&);

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::FinalizeSameDocumentNavigationRequest const&);
template<>
WEB_API ErrorOr<Web::FinalizeSameDocumentNavigationRequest> decode(Decoder&);

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::DefinitelyCloseTopLevelTraversableRequest const&);
template<>
WEB_API ErrorOr<Web::DefinitelyCloseTopLevelTraversableRequest> decode(Decoder&);

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::FlushSessionHistoryTraversalQueueForTestingRequest const&);
template<>
WEB_API ErrorOr<Web::FlushSessionHistoryTraversalQueueForTestingRequest> decode(Decoder&);

}
