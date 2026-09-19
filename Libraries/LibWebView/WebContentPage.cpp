/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/CanonicalTraversable.h>
#include <LibWebView/ViewImplementation.h>
#include <LibWebView/WebContentClient.h>
#include <LibWebView/WebContentPage.h>

namespace WebView {

bool WebContentPage::is_open() const
{
    return client && client->is_page_open(id);
}

CanonicalTraversable* WebContentPage::traversable() const
{
    return client ? client->traversable_for_page(id) : nullptr;
}

Optional<ViewImplementation&> WebContentPage::view() const
{
    if (!client)
        return {};
    return client->view_displaying_page(id);
}

Optional<ViewImplementation&> WebContentPage::owning_view() const
{
    auto* traversable = this->traversable();
    if (!traversable)
        return {};
    return ViewImplementation::find_view_for_traversable(*traversable);
}

Optional<CanonicalNavigable&> WebContentPage::hosted_navigable(Web::HTML::CrossProcessId navigable_id) const
{
    if (!client)
        return {};
    return client->hosted_navigable_for_page(id, navigable_id);
}

bool WebContentPage::needs_beforeunload_check() const
{
    return client && client->page_needs_beforeunload_check(id);
}

}
