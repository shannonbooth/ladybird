/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 * Copyright (c) 2022, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibURL/URL.h>
#include <LibWebCommon/Export.h>

namespace Web::HTML {

WEBCOMMON_API bool can_have_its_url_rewritten(URL::URL const& document_url, URL::URL const& target_url);

}
