/*
 * Copyright (c) 2022, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Assertions.h>
#include <AK/Vector.h>
#include <LibJS/Runtime/PropertyKey.h>
#include <LibURL/Origin.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/BrowsingContext.h>
#include <LibWeb/HTML/CrossOrigin/AbstractOperations.h>
#include <LibWeb/HTML/CrossOrigin/Reporting.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/origin.html#coop-check-access-report
void check_if_access_between_two_browsing_contexts_should_be_reported(
    GC::Ptr<BrowsingContext const> accessor,
    GC::Ptr<BrowsingContext const> accessed,
    JS::PropertyKey const& property_key,
    GC::Ref<EnvironmentSettingsObject const> environment)
{
    // FIXME: Spec bug: https://github.com/whatwg/html/issues/10192
    if (!accessor || !accessed)
        return;

    // 1. If propertyKey is not a cross-origin accessible window property name, then return.
    if (!is_cross_origin_accessible_window_property_name(property_key))
        return;

    // 2. Assert: accessor's active document and accessed's active document are both fully active.
    if (!accessor->active_document_is_fully_active() || !accessed->active_document_is_fully_active())
        return;

    // 3. Let accessorTopDocument be accessor's top-level browsing context's active document.
    auto accessor_top_level_browsing_context = accessor->top_level_browsing_context();
    if (!accessor_top_level_browsing_context)
        return;

    auto accessor_top_document_origin = accessor_top_level_browsing_context->active_document_origin();
    if (!accessor_top_document_origin.has_value())
        return;

    // 4. Let accessorInclusiveAncestorOrigins be the list obtained by taking the origin of the active document of each of accessor's active document's inclusive ancestor navigables.
    Vector<URL::Origin> accessor_inclusive_ancestor_origins = {};
    for (auto ancestor = accessor; ancestor; ancestor = ancestor->parent_browsing_context()) {
        auto origin = ancestor->active_document_origin();
        if (origin.has_value())
            accessor_inclusive_ancestor_origins.append(origin.release_value());
    }

    // 5. Let accessedTopDocument be accessed's top-level browsing context's active document.
    auto accessed_top_level_browsing_context = accessed->top_level_browsing_context();
    if (!accessed_top_level_browsing_context)
        return;

    auto accessed_top_document_origin = accessed_top_level_browsing_context->active_document_origin();
    if (!accessed_top_document_origin.has_value())
        return;

    // 6. Let accessedInclusiveAncestorOrigins be the list obtained by taking the origin of the active document of each of accessed's active document's inclusive ancestor navigables.
    Vector<URL::Origin> accessed_inclusive_ancestor_origins = {};
    for (auto ancestor = accessed; ancestor; ancestor = ancestor->parent_browsing_context()) {
        auto origin = ancestor->active_document_origin();
        if (origin.has_value())
            accessed_inclusive_ancestor_origins.append(origin.release_value());
    }

    // 7. If any of accessorInclusiveAncestorOrigins are not same origin with accessorTopDocument's origin, or if any of accessedInclusiveAncestorOrigins are not same origin with accessedTopDocument's origin, then return.
    for (auto const& origin : accessor_inclusive_ancestor_origins)
        if (!origin.is_same_origin(accessor_top_document_origin.value()))
            return;
    for (auto const& origin : accessed_inclusive_ancestor_origins)
        if (!origin.is_same_origin(accessed_top_document_origin.value()))
            return;

    // 8. If accessor's top-level browsing context's virtual browsing context group ID is accessed's top-level browsing context's virtual browsing context group ID, then return.
    if (accessor_top_level_browsing_context && accessed_top_level_browsing_context && accessor_top_level_browsing_context->virtual_browsing_context_group_id() == accessed_top_level_browsing_context->virtual_browsing_context_group_id())
        return;

    // 9. Let accessorAccessedRelationship be a new accessor-accessed relationship with value none.
    auto accessor_accessed_relationship = AccessorAccessedRelationship::None;

    // 10. If accessed's top-level browsing context's opener browsing context is accessor or is an ancestor of accessor, then set accessorAccessedRelationship to accessor is opener.
    if (auto opener = accessed_top_level_browsing_context->opener_browsing_context()) {
        if (opener == accessor || opener->is_ancestor_of(*accessor))
            accessor_accessed_relationship = AccessorAccessedRelationship::AccessorIsOpener;
    }

    // 11. If accessor's top-level browsing context's opener browsing context is accessed or is an ancestor of accessed, then set accessorAccessedRelationship to accessor is openee.
    if (auto opener = accessor_top_level_browsing_context->opener_browsing_context()) {
        if (opener == accessed || opener->is_ancestor_of(*accessed))
            accessor_accessed_relationship = AccessorAccessedRelationship::AccessorIsOpenee;
    }

    // 12. Queue violation reports for accesses, given accessorAccessedRelationship, accessorTopDocument's opener policy, accessedTopDocument's opener policy, accessor's active document's URL, accessed's active document's URL, accessor's top-level browsing context's initial URL, accessed's top-level browsing context's initial URL, accessor's active document's origin, accessed's active document's origin, accessor's top-level browsing context's opener origin at creation, accessed's top-level browsing context's opener origin at creation, accessorTopDocument's referrer, accessedTopDocument's referrer, propertyKey, and environment.
    (void)environment;
    (void)accessor_accessed_relationship;
}

}
