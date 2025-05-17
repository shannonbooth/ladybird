/*
 * Copyright (c) 2024, Andrew Kaster <andrew@ladybird.org>
 * Copyright (c) 2025, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibIPC/Decoder.h>
#include <LibIPC/Encoder.h>
#include <LibWeb/ServiceWorker/ServiceWorkerRecord.h>

namespace IPC {

template<>
ErrorOr<void> encode(Encoder& encoder, Web::ServiceWorker::SerializedServiceWorkerRecord const& object)
{
    TRY(encoder.encode(object.script_url));
    return {};
}

template<>
ErrorOr<Web::ServiceWorker::SerializedServiceWorkerRecord> decode(Decoder& decoder)
{
    Web::ServiceWorker::SerializedServiceWorkerRecord object {};
    object.script_url = TRY(decoder.decode<URL::URL>());
    return object;
}

}
