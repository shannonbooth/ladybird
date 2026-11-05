/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/CanonicalTraversable.h>
#include <LibWebView/UserAgentTopLevelTraversableSet.h>

namespace WebView {

UserAgentTopLevelTraversableSet& UserAgentTopLevelTraversableSet::the()
{
    // The user agent owns the set for the lifetime of the application.
    static auto* set = new UserAgentTopLevelTraversableSet;
    return *set;
}

UserAgentTopLevelTraversableSet::~UserAgentTopLevelTraversableSet() = default;

CanonicalTraversable& UserAgentTopLevelTraversableSet::add(Badge<CanonicalTraversable>, NonnullOwnPtr<CanonicalTraversable> traversable)
{
    VERIFY(!traversable_containing(traversable->id()));
    auto& result = *traversable;
    m_traversables.append(move(traversable));
    return result;
}

void UserAgentTopLevelTraversableSet::remove(Badge<CanonicalTraversable>, CanonicalTraversable& traversable)
{
    m_traversables.remove_first_matching([&](auto const& entry) { return entry.ptr() == &traversable; });
}

void UserAgentTopLevelTraversableSet::for_each(Function<IterationDecision(CanonicalTraversable&)> const& callback) const
{
    for (auto const& traversable : m_traversables) {
        if (callback(*traversable) == IterationDecision::Break)
            break;
    }
}

CanonicalTraversable* UserAgentTopLevelTraversableSet::find(TabId id) const
{
    for (auto const& traversable : m_traversables) {
        if (traversable->tab_id() == id)
            return traversable.ptr();
    }
    return nullptr;
}

CanonicalTraversable* UserAgentTopLevelTraversableSet::find_by_handle(StringView handle) const
{
    for (auto const& traversable : m_traversables) {
        if (traversable->handle() == handle)
            return traversable.ptr();
    }
    return nullptr;
}

CanonicalTraversable* UserAgentTopLevelTraversableSet::traversable_containing(Web::HTML::CrossProcessId id) const
{
    CanonicalTraversable* result = nullptr;
    for_each([&](auto& traversable) {
        if (!traversable.find(id).has_value())
            return IterationDecision::Continue;
        result = &traversable;
        return IterationDecision::Break;
    });
    return result;
}

CanonicalNavigable* UserAgentTopLevelTraversableSet::navigable_with_active_browsing_context(CanonicalBrowsingContext const& context) const
{
    CanonicalNavigable* result = nullptr;
    for_each([&](auto& traversable) {
        traversable.for_each_in_inclusive_subtree([&](CanonicalNavigable& navigable) {
            if (&navigable.active_browsing_context() != &context)
                return IterationDecision::Continue;
            result = &navigable;
            return IterationDecision::Break;
        });
        return result ? IterationDecision::Break : IterationDecision::Continue;
    });
    return result;
}

}
