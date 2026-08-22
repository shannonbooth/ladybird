/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/HTML/SessionHistoryEntry.h>

namespace Web::HTML {

struct SameDocumentSessionHistoryEntryDescriptor {
    URL::URL url;
    CrossProcessId document_state_id;
    StorageSerializationRecord classic_history_api_state;
    StorageSerializationRecord navigation_api_state;
    Utf16String navigation_api_key;
    Utf16String navigation_api_id;
    ScrollRestorationMode scroll_restoration_mode { ScrollRestorationMode::Auto };
    SessionHistoryEntryScrollPositionData scroll_position_data;
};

WEB_API SameDocumentSessionHistoryEntryDescriptor create_same_document_session_history_entry_descriptor(SessionHistoryEntry const&);

}

namespace IPC {

template<>
WEB_API ErrorOr<void> encode(Encoder&, Web::HTML::SameDocumentSessionHistoryEntryDescriptor const&);

template<>
WEB_API ErrorOr<Web::HTML::SameDocumentSessionHistoryEntryDescriptor> decode(Decoder&);

}
