/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullRefPtr.h>
#include <AK/RefCounted.h>
#include <AK/RefPtr.h>
#include <AK/Weakable.h>
#include <LibURL/Origin.h>
#include <LibWebView/Export.h>
#include <LibWebView/Forward.h>

namespace WebView {

// https://dom.spec.whatwg.org/#concept-document
// NB: The Document is in the process hosting its relevant agent. The UI process models what placing the Document
//     and its browsing context depends on.
class WEBVIEW_API CanonicalDocument final
    : public RefCounted<CanonicalDocument>
    , public Weakable<CanonicalDocument> {
public:
    enum class IsInitialAboutBlank : bool {
        No,
        Yes,
    };

    static NonnullRefPtr<CanonicalDocument> create(URL::Origin, NonnullRefPtr<CanonicalBrowsingContext>, NonnullRefPtr<CanonicalWindow>, IsInitialAboutBlank);

    ~CanonicalDocument();

    // https://dom.spec.whatwg.org/#concept-document-origin
    URL::Origin const& origin() const { return m_origin; }

    // https://html.spec.whatwg.org/multipage/document-sequences.html#concept-document-bc
    CanonicalBrowsingContext& browsing_context() const { return m_browsing_context; }

    // https://html.spec.whatwg.org/multipage/webappapis.html#concept-relevant-global
    CanonicalWindow& relevant_global_object() const { return m_relevant_global_object; }

    // https://html.spec.whatwg.org/multipage/dom.html#is-initial-about:blank
    bool is_initial_about_blank() const { return m_is_initial_about_blank == IsInitialAboutBlank::Yes; }

    void make_active();

    RefPtr<WebContentPage> const& page_created_in() const { return m_page_created_in; }
    RefPtr<WebContentPage> host() const;
    void set_host(WebContentPage&);
    bool unload_started() const { return m_unload_started; }
    void set_unload_started() { m_unload_started = true; }
    bool is_unloaded() const { return m_is_unloaded; }
    void set_unloaded() { m_is_unloaded = true; }

private:
    CanonicalDocument(URL::Origin, NonnullRefPtr<CanonicalBrowsingContext>, NonnullRefPtr<CanonicalWindow>, IsInitialAboutBlank);

    URL::Origin m_origin;
    NonnullRefPtr<CanonicalBrowsingContext> m_browsing_context;
    NonnullRefPtr<CanonicalWindow> m_relevant_global_object;
    IsInitialAboutBlank m_is_initial_about_blank { IsInitialAboutBlank::No };
    RefPtr<WebContentPage> m_page_created_in;
    bool m_unload_started { false };
    bool m_is_unloaded { false };
};

}
