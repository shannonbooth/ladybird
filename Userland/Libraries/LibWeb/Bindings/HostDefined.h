/*
 * Copyright (c) 2022, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/TypeCasts.h>
#include <LibJS/Heap/GCPtr.h>
#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Forward.h>
#include <LibWeb/HTML/Scripting/SyntheticRealmSettings.h>

namespace Web::Bindings {

struct HostDefined : public JS::Realm::HostDefined {
    HostDefined(JS::GCPtr<HTML::EnvironmentSettingsObject> eso, JS::NonnullGCPtr<Intrinsics> intrinsics, JS::GCPtr<Page> page, OwnPtr<HTML::SyntheticRealmSettings> synthetic_realm_settings)
        : environment_settings_object(eso)
        , intrinsics(intrinsics)
        , page(page)
        , synthetic_realm_settings(move(synthetic_realm_settings))
    {
    }
    virtual ~HostDefined() override;
    virtual void visit_edges(JS::Cell::Visitor& visitor) override;

    JS::GCPtr<HTML::EnvironmentSettingsObject> environment_settings_object;
    JS::NonnullGCPtr<Intrinsics> intrinsics;
    JS::GCPtr<Page> page;
    OwnPtr<HTML::SyntheticRealmSettings> synthetic_realm_settings;
};

[[nodiscard]] inline HTML::SyntheticRealmSettings* host_defined_synthetic_realm_settings(JS::Realm& realm)
{
    return verify_cast<HostDefined>(realm.host_defined())->synthetic_realm_settings.ptr();
}

[[nodiscard]] inline HTML::SyntheticRealmSettings const* host_defined_synthetic_realm_settings(JS::Realm const& realm)
{
    return verify_cast<HostDefined>(realm.host_defined())->synthetic_realm_settings.ptr();
}

[[nodiscard]] inline HTML::EnvironmentSettingsObject& host_defined_environment_settings_object(JS::Realm& realm)
{
    return *verify_cast<HostDefined>(realm.host_defined())->environment_settings_object;
}

[[nodiscard]] inline HTML::EnvironmentSettingsObject const& host_defined_environment_settings_object(JS::Realm const& realm)
{
    return *verify_cast<HostDefined>(realm.host_defined())->environment_settings_object;
}

[[nodiscard]] inline Page& host_defined_page(JS::Realm& realm)
{
    return *verify_cast<HostDefined>(realm.host_defined())->page;
}

}
