/*
 * Copyright (c) 2022-2023, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/String.h>
#include <AK/Variant.h>
#include <AK/Vector.h>
#include <LibHTTP/HeaderList.h>
#include <LibWeb/Bindings/Headers.h>
#include <LibWeb/Bindings/RustInterface.h>
#include <LibWeb/Export.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

namespace Web::Fetch {

using HeadersInit = Variant<Vector<Vector<String>>, OrderedHashMap<String, String>>;

// https://fetch.spec.whatwg.org/#headers-class
// The C++ side of a Headers object, which is implemented in Rust (Libraries/LibWeb/Bindings/Rust/src/fetch): what
// fetching creates for its requests and responses, and changes.
class WEB_API Headers final : public Bindings::RustInterfaceCell {
    WEB_RUST_INTERFACE(Headers, headers)

public:
    enum class Guard : u8 {
        Immutable,
        Request,
        RequestNoCORS,
        Response,
        None,
    };

    [[nodiscard]] static GC::Ref<Headers> create(NonnullRefPtr<HTTP::HeaderList>);

    [[nodiscard]] NonnullRefPtr<HTTP::HeaderList> header_list() const;

    [[nodiscard]] Guard guard() const;
    void set_guard(Guard);

    WebIDL::ExceptionOr<void> fill(HeadersInit const&);
    WebIDL::ExceptionOr<void> append(HTTP::Header);
};

}
