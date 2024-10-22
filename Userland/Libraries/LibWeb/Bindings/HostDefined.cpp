/*
 * Copyright (c) 2022, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Heap/Cell.h>
#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/HostDefined.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Scripting/SyntheticRealmSettings.h>
#include <LibWeb/Page/Page.h>

namespace Web::Bindings {

HostDefined::~HostDefined() = default;

void HostDefined::visit_edges(JS::Cell::Visitor& visitor)
{
    JS::Realm::HostDefined::visit_edges(visitor);
    visitor.visit(environment_settings_object);
    visitor.visit(intrinsics);
    visitor.visit(page);
    if (synthetic_realm_settings)
        synthetic_realm_settings->visit_edges(visitor);
}

}
