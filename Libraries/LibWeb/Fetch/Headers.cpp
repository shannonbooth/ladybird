/*
 * Copyright (c) 2022-2023, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/HostClassBuilder.h>
#include <LibJS/Runtime/VM.h>
#include <LibTextCodec/Encoder.h>
#include <LibWeb/Fetch/Headers.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/CORS.h>

namespace Web::Fetch {

using OnBytes = void (*)(void* ctx, ReadonlyBytes);
using OnHeader = void (*)(void* ctx, ReadonlyBytes name, ReadonlyBytes value);

static HTTP::HeaderList& header_list_from_abi(void* list)
{
    return *static_cast<HTTP::HeaderList*>(list);
}

extern "C" {

// Called from Bindings/Rust/src/fetch/headers.rs.
void* web_fetch_header_list_create();
void web_fetch_header_list_unref(void* list);
bool web_fetch_header_list_contains(void* list, ReadonlyBytes name);
bool web_fetch_header_list_get(void* list, ReadonlyBytes name, void* ctx, OnBytes on_value);
void web_fetch_header_list_delete(void* list, ReadonlyBytes name);
void web_fetch_header_list_set(void* list, ReadonlyBytes name, ReadonlyBytes value);
void web_fetch_header_list_append(void* list, ReadonlyBytes name, ReadonlyBytes value);
void web_fetch_header_list_for_each(void* list, void* ctx, OnHeader on_header);
void web_fetch_header_list_sort_and_combine(void* list, void* ctx, OnHeader on_header);

// Called from Bindings/Rust/src/fetch/infrastructure/http/headers.rs.
bool web_fetch_is_header_name(ReadonlyBytes name);
bool web_fetch_is_header_value(ReadonlyBytes value);
bool web_fetch_is_forbidden_request_header(ReadonlyBytes name, ReadonlyBytes value);
bool web_fetch_is_forbidden_response_header_name(ReadonlyBytes name);
bool web_fetch_is_no_cors_safelisted_request_header_name(ReadonlyBytes name);
bool web_fetch_is_privileged_no_cors_request_header_name(ReadonlyBytes name);
bool web_fetch_is_no_cors_safelisted_request_header(ReadonlyBytes name, ReadonlyBytes value);

// Defined in Bindings/Rust/src/fetch/exports.rs.
void* web_headers_create(JSVM*, void* header_list);
void* web_headers_header_list(void* headers);
u8 web_headers_guard(void* headers);
void web_headers_set_guard(void* headers, u8 guard);
bool web_headers_append(void* headers, ReadonlyBytes name, ReadonlyBytes value, ReadonlyBytes* error);
bool web_headers_fill(void* headers, bool is_record, ReadonlySpan<ReadonlySpan<ReadonlyBytes>> entries, ReadonlyBytes* error);
}

extern "C" void* web_fetch_header_list_create()
{
    return &HTTP::HeaderList::create().leak_ref();
}

extern "C" void web_fetch_header_list_unref(void* list)
{
    header_list_from_abi(list).unref();
}

extern "C" bool web_fetch_header_list_contains(void* list, ReadonlyBytes name)
{
    return header_list_from_abi(list).contains(name);
}

extern "C" bool web_fetch_header_list_get(void* list, ReadonlyBytes name, void* ctx, OnBytes on_value)
{
    auto value = header_list_from_abi(list).get(name);
    if (!value.has_value())
        return false;
    on_value(ctx, value->bytes());
    return true;
}

extern "C" void web_fetch_header_list_delete(void* list, ReadonlyBytes name)
{
    header_list_from_abi(list).delete_(name);
}

extern "C" void web_fetch_header_list_set(void* list, ReadonlyBytes name, ReadonlyBytes value)
{
    header_list_from_abi(list).set({ ByteString { name }, ByteString { value } });
}

extern "C" void web_fetch_header_list_append(void* list, ReadonlyBytes name, ReadonlyBytes value)
{
    header_list_from_abi(list).append({ ByteString { name }, ByteString { value } });
}

extern "C" void web_fetch_header_list_for_each(void* list, void* ctx, OnHeader on_header)
{
    for (auto const& header : header_list_from_abi(list))
        on_header(ctx, header.name.bytes(), header.value.bytes());
}

extern "C" void web_fetch_header_list_sort_and_combine(void* list, void* ctx, OnHeader on_header)
{
    for (auto const& header : header_list_from_abi(list).sort_and_combine())
        on_header(ctx, header.name.bytes(), header.value.bytes());
}

extern "C" bool web_fetch_is_header_name(ReadonlyBytes name)
{
    return HTTP::is_header_name(name);
}

extern "C" bool web_fetch_is_header_value(ReadonlyBytes value)
{
    return HTTP::is_header_value(value);
}

extern "C" bool web_fetch_is_forbidden_request_header(ReadonlyBytes name, ReadonlyBytes value)
{
    return HTTP::is_forbidden_request_header({ ByteString { name }, ByteString { value } });
}

extern "C" bool web_fetch_is_forbidden_response_header_name(ReadonlyBytes name)
{
    return HTTP::is_forbidden_response_header_name(name);
}

extern "C" bool web_fetch_is_no_cors_safelisted_request_header_name(ReadonlyBytes name)
{
    return Infrastructure::is_no_cors_safelisted_request_header_name(name);
}

extern "C" bool web_fetch_is_privileged_no_cors_request_header_name(ReadonlyBytes name)
{
    return Infrastructure::is_privileged_no_cors_request_header_name(name);
}

extern "C" bool web_fetch_is_no_cors_safelisted_request_header(ReadonlyBytes name, ReadonlyBytes value)
{
    return Infrastructure::is_no_cors_safelisted_request_header({ ByteString { name }, ByteString { value } });
}

static WebIDL::ExceptionOr<void> type_error_unless(bool succeeded, ReadonlyBytes error)
{
    if (succeeded)
        return {};
    return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, Utf16String::from_utf8_without_validation(error) };
}

GC::Ref<Headers> Headers::create(NonnullRefPtr<HTTP::HeaderList> header_list)
{
    return GC::Ref { *static_cast<Headers*>(web_headers_create(JS::HostABI::vm_to_abi(JS::VM::the()), &header_list.leak_ref())) };
}

NonnullRefPtr<HTTP::HeaderList> Headers::header_list() const
{
    return header_list_from_abi(web_headers_header_list(const_cast<Headers*>(this)));
}

Headers::Guard Headers::guard() const
{
    return static_cast<Guard>(web_headers_guard(const_cast<Headers*>(this)));
}

void Headers::set_guard(Guard guard)
{
    web_headers_set_guard(this, to_underlying(guard));
}

// https://fetch.spec.whatwg.org/#concept-headers-fill
WebIDL::ExceptionOr<void> Headers::fill(HeadersInit const& object)
{
    Vector<Vector<ByteString>> strings;
    auto is_record = object.visit(
        [&](Vector<Vector<String>> const& sequence) {
            for (auto const& header : sequence) {
                Vector<ByteString> encoded_header;
                for (auto const& string : header)
                    encoded_header.append(TextCodec::isomorphic_encode(string));
                strings.append(move(encoded_header));
            }
            return false;
        },
        [&](OrderedHashMap<String, String> const& record) {
            for (auto const& [key, value] : record)
                strings.append({ TextCodec::isomorphic_encode(key), TextCodec::isomorphic_encode(value) });
            return true;
        });

    Vector<Vector<ReadonlyBytes>> entry_spans;
    entry_spans.ensure_capacity(strings.size());
    for (auto const& entry : strings) {
        Vector<ReadonlyBytes> string_spans;
        for (auto const& string : entry)
            string_spans.append(string.bytes());
        entry_spans.unchecked_append(move(string_spans));
    }

    Vector<ReadonlySpan<ReadonlyBytes>> entries;
    entries.ensure_capacity(entry_spans.size());
    for (auto const& entry : entry_spans)
        entries.unchecked_append(entry.span());

    ReadonlyBytes error;
    return type_error_unless(web_headers_fill(this, is_record, entries.span(), &error), error);
}

// https://fetch.spec.whatwg.org/#concept-headers-append
WebIDL::ExceptionOr<void> Headers::append(HTTP::Header header)
{
    ReadonlyBytes error;
    return type_error_unless(web_headers_append(this, header.name.bytes(), header.value.bytes(), &error), error);
}

}
