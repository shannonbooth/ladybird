/*
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGC/Ptr.h>
#include <LibJS/Forward.h>
#include <LibJS/Runtime/Value.h>
#include <LibWeb/Forward.h>

namespace Web::Streams {

// https://streams.spec.whatwg.org/#transferrable-streams
//
// 8.2. Transferable streams
//
// Transferable streams are implemented using a special kind of identity transform which has the
// writable side in one realm and the readable side in another realm. The following abstract
// operations are used to implement these "cross-realm transforms".

void cross_realm_transform_send_error(GC::Ref<HTML::MessagePort>, JS::Value error);
WebIDL::ExceptionOr<void> pack_and_post_message(GC::Ref<HTML::MessagePort>, GC::Ref<JS::PrimitiveString> type, JS::Value value);
WebIDL::ExceptionOr<void> pack_and_post_message_handling_error(GC::Ref<HTML::MessagePort>, GC::Ref<JS::PrimitiveString> type, JS::Value value);
void set_up_cross_realm_transform_readable(GC::Ref<ReadableStream>, GC::Ref<HTML::MessagePort>);
void set_up_cross_realm_transform_writable(GC::Ref<WritableStream>, GC::Ref<HTML::MessagePort>);

}
