/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Variant.h>
#include <LibGC/Ptr.h>
#include <LibWeb/Forward.h>
#include <LibWeb/Geolocation/GeolocationPositionError.h>

namespace Web::Geolocation {

// https://w3c.github.io/geolocation/#dfn-emulated-position-data
using EmulatedPositionData = Variant<Empty, GC::Ref<GeolocationCoordinates>, GeolocationPositionError::ErrorCode>;

}
