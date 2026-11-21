/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Types.h>

namespace Web::HTML {

// The operation that ran the URL and history update steps, or navigated to a fragment, creating a same-document entry.
// Each limits the entry's URL in its own way.
enum class SameDocumentEntrySource : u8 {
    // https://html.spec.whatwg.org/multipage/nav-history-apis.html#shared-history-push/replace-state-steps
    HistoryAPI,
    // https://html.spec.whatwg.org/multipage/nav-history-apis.html#inner-navigate-event-firing-algorithm
    NavigationAPIIntercept,
    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#navigate-fragid
    FragmentNavigation,
    // https://html.spec.whatwg.org/multipage/dynamic-markup-insertion.html#document-open-steps
    DocumentOpen,
    // https://html.spec.whatwg.org/multipage/nav-history-apis.html#window-open-steps
    // https://html.spec.whatwg.org/multipage/iframe-embed-object.html#shared-attribute-processing-steps-for-iframe-and-frame-elements
    AboutBlank,
};

}
