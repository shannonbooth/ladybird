/*
 * Copyright (c) 2023, Matthew Olsson <mattco@serenityos.org>
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/NativeFunction.h>
#include <LibJS/Runtime/PromiseCapability.h>
#include <LibWeb/Bindings/ExceptionOrUtils.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/WritableStreamPrototype.h>
#include <LibWeb/DOM/IDLEventListener.h>
#include <LibWeb/HTML/EventNames.h>
#include <LibWeb/HTML/MessagePort.h>
#include <LibWeb/HTML/StructuredSerializeOptions.h>
#include <LibWeb/Streams/AbstractOperations.h>
#include <LibWeb/Streams/UnderlyingSink.h>
#include <LibWeb/Streams/WritableStream.h>
#include <LibWeb/Streams/WritableStreamDefaultController.h>
#include <LibWeb/Streams/WritableStreamDefaultWriter.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

namespace Web::Streams {

GC_DEFINE_ALLOCATOR(WritableStream);

// https://streams.spec.whatwg.org/#ws-constructor
WebIDL::ExceptionOr<GC::Ref<WritableStream>> WritableStream::construct_impl(JS::Realm& realm, Optional<GC::Root<JS::Object>> const& underlying_sink_object, QueuingStrategy const& strategy)
{
    auto& vm = realm.vm();

    auto writable_stream = realm.create<WritableStream>(realm);

    // 1. If underlyingSink is missing, set it to null.
    auto underlying_sink = underlying_sink_object.has_value() ? JS::Value(underlying_sink_object.value()) : JS::js_null();

    // 2. Let underlyingSinkDict be underlyingSink, converted to an IDL value of type UnderlyingSink.
    auto underlying_sink_dict = TRY(UnderlyingSink::from_value(vm, underlying_sink));

    // 3. If underlyingSinkDict["type"] exists, throw a RangeError exception.
    if (underlying_sink_dict.type.has_value())
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::RangeError, "Invalid use of reserved key 'type'"sv };

    // 4. Perform ! InitializeWritableStream(this).
    // Note: This AO configures slot values which are already specified in the class's field initializers.

    // 5. Let sizeAlgorithm be ! ExtractSizeAlgorithm(strategy).
    auto size_algorithm = extract_size_algorithm(vm, strategy);

    // 6. Let highWaterMark be ? ExtractHighWaterMark(strategy, 1).
    auto high_water_mark = TRY(extract_high_water_mark(strategy, 1));

    // 7. Perform ? SetUpWritableStreamDefaultControllerFromUnderlyingSink(this, underlyingSink, underlyingSinkDict, highWaterMark, sizeAlgorithm).
    TRY(set_up_writable_stream_default_controller_from_underlying_sink(*writable_stream, underlying_sink, underlying_sink_dict, high_water_mark, move(size_algorithm)));

    return writable_stream;
}

// https://streams.spec.whatwg.org/#ws-locked
bool WritableStream::locked() const
{
    // 1. Return ! IsWritableStreamLocked(this).
    return is_writable_stream_locked(*this);
}

// https://streams.spec.whatwg.org/#ws-close
GC::Ref<WebIDL::Promise> WritableStream::close()
{
    auto& realm = this->realm();

    // 1. If ! IsWritableStreamLocked(this) is true, return a promise rejected with a TypeError exception.
    if (is_writable_stream_locked(*this)) {
        auto exception = JS::TypeError::create(realm, "Cannot close a locked stream"sv);
        return WebIDL::create_rejected_promise(realm, exception);
    }

    // 2. If ! WritableStreamCloseQueuedOrInFlight(this) is true, return a promise rejected with a TypeError exception.
    if (writable_stream_close_queued_or_in_flight(*this)) {
        auto exception = JS::TypeError::create(realm, "Cannot close a stream that is already closed or errored"sv);
        return WebIDL::create_rejected_promise(realm, exception);
    }

    // 3. Return ! WritableStreamClose(this).
    return writable_stream_close(*this);
}

// https://streams.spec.whatwg.org/#ws-abort
GC::Ref<WebIDL::Promise> WritableStream::abort(JS::Value reason)
{
    auto& realm = this->realm();

    // 1. If ! IsWritableStreamLocked(this) is true, return a promise rejected with a TypeError exception.
    if (is_writable_stream_locked(*this)) {
        auto exception = JS::TypeError::create(realm, "Cannot abort a locked stream"sv);
        return WebIDL::create_rejected_promise(realm, exception);
    }

    // 2. Return ! WritableStreamAbort(this, reason).
    return writable_stream_abort(*this, reason);
}

// https://streams.spec.whatwg.org/#ws-get-writer
WebIDL::ExceptionOr<GC::Ref<WritableStreamDefaultWriter>> WritableStream::get_writer()
{
    // 1. Return ? AcquireWritableStreamDefaultWriter(this).
    return acquire_writable_stream_default_writer(*this);
}

struct TransformForWritableAlgorithmState : GC::Cell {
    GC_CELL(UpdateAlgorithmState, JS::Cell);

    GC::Ptr<WebIDL::Promise> backpressure_promise;

private:
    virtual void visit_edges(JS::Cell::Visitor& visitor) override
    {
        GC::Cell::visit_edges(visitor);
        visitor.visit(backpressure_promise);
    }
};

// https://streams.spec.whatwg.org/#abstract-opdef-packandpostmessage
static WebIDL::ExceptionOr<void> pack_and_post_message(GC::Ref<HTML::MessagePort> port, GC::Ref<JS::PrimitiveString> type, JS::Value value)
{
    auto& vm = port->vm();

    // 1. Let message be OrdinaryObjectCreate(null).
    auto message = JS::Object::create(port->realm(), nullptr);

    // 2. Perform ! CreateDataProperty(message, "type", type).
    MUST(message->create_data_property(vm.names.type, type));

    // 3. Perform ! CreateDataProperty(message, "value", value).
    MUST(message->create_data_property(vm.names.value, value));

    // 4. Let targetPort be the port with which port is entangled, if any; otherwise let it be null.
    auto target_port = port->entangled_port();

    // 5. Let options be «[ "transfer" → « » ]».
    HTML::StructuredSerializeOptions options;

    // 6. Run the message port post message steps providing targetPort, message, and options.
    return port->message_port_post_message_steps(target_port, message, options);
}

// https://streams.spec.whatwg.org/#abstract-opdef-crossrealmtransformsenderror
static void cross_realm_transform_send_error(GC::Ref<HTML::MessagePort> port, JS::Value error)
{
    // 1. Perform PackAndPostMessage(port, "error", error), discarding the result.
    (void)pack_and_post_message(port, JS::PrimitiveString::create(port->vm(), "error"sv), error);
}

// https://streams.spec.whatwg.org/#abstract-opdef-packandpostmessagehandlingerror
static WebIDL::ExceptionOr<void> pack_and_post_message_handling_error(GC::Ref<HTML::MessagePort> port, GC::Ref<JS::PrimitiveString> type, JS::Value value)
{
    // 1. Let result be PackAndPostMessage(port, type, value).
    auto result = pack_and_post_message(port, type, value);

    // 2. If result is an abrupt completion,
    if (result.is_exception()) {
        // 1. Perform ! CrossRealmTransformSendError(port, result.[[Value]]).
        cross_realm_transform_send_error(port, Bindings::dom_exception_to_throw_completion(port->vm(), result.release_error()).release_value().value());
    }

    // 3. Return result as a completion record.
    return result;
}

// https://streams.spec.whatwg.org/#abstract-opdef-setupcrossrealmtransformwritable
void WritableStream::set_up_cross_realm_transform_writable(GC::Ref<HTML::MessagePort> port)
{
    auto& vm = this->vm();
    auto& realm = this->realm();

    // 1. Perform ! InitializeWritableStream(stream).
    // NOTE: Done by the constructor.

    // 2. Let controller be a new WritableStreamDefaultController.
    auto controller = realm.create<WritableStreamDefaultController>(realm);

    // 3. Let backpressurePromise be a new promise.
    auto state = realm.heap().allocate<TransformForWritableAlgorithmState>();
    state->backpressure_promise = WebIDL::create_promise(realm);

    // 4. Add a handler for port’s message event with the following steps:
    auto message_handler = JS::NativeFunction::create(realm, [&realm, controller, state](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        // 1. Let data be the data of the message.
        auto data = vm.argument(0);

        // 2. Assert: data is an Object.
        VERIFY(data.is_object());

        // 3. Let type be ! Get(data, "type").
        auto type = MUST(data.get(vm, vm.names.type));

        // 4. Let value be ! Get(data, "value").
        auto value = MUST(data.get(vm, vm.names.value));

        // 5. Assert: type is a String.
        VERIFY(type.is_string());

        // 6. If type is "pull",
        if (type.as_string().utf8_string_view() == "pull"sv) {
            // 1. If backpressurePromise is not undefined,
            if (state->backpressure_promise) {
                // 1. Resolve backpressurePromise with undefined.
                WebIDL::resolve_promise(realm, *state->backpressure_promise, JS::js_undefined());

                // 2. Set backpressurePromise to undefined.
                state->backpressure_promise = nullptr;
            }
        }

        // 7. Otherwise, if type is "error",
        else if (type.as_string().utf8_string_view() == "error"sv) {
            // 1. Perform ! WritableStreamDefaultControllerErrorIfNeeded(controller, value).
            writable_stream_default_controller_error_if_needed(controller, value);

            // 2. If backpressurePromise is not undefined,
            if (state->backpressure_promise) {
                // 1. Resolve backpressurePromise with undefined.
                WebIDL::resolve_promise(realm, *state->backpressure_promise, JS::js_undefined());

                // 2. Set backpressurePromise to undefined.
                state->backpressure_promise = nullptr;
            }
        }

        return JS::js_undefined(); }, 0, "", &realm);
    auto message_callback = realm.heap().allocate<WebIDL::CallbackType>(*message_handler, realm);
    port->add_event_listener_without_options(HTML::EventNames::message, DOM::IDLEventListener::create(realm, message_callback));

    // 5. Add a handler for port’s messageerror event with the following steps:
    auto messageerror_handler = JS::NativeFunction::create(realm, [&realm, controller, port](JS::VM&) -> JS::ThrowCompletionOr<JS::Value> {
        // 1. Let error be a new "DataCloneError" DOMException.
        auto error = WebIDL::DataCloneError::create(realm, "Error transferring stream"_string);

        // 2. Perform ! CrossRealmTransformSendError(port, error).
        cross_realm_transform_send_error(port, error);

        // 3. Perform ! WritableStreamDefaultControllerErrorIfNeeded(controller, error).
        writable_stream_default_controller_error_if_needed(controller, error);

        // 4. Disentangle port.
        port->disentangle();

        return JS::js_undefined(); }, 0, "", &realm);
    auto messageerror_callback = realm.heap().allocate<WebIDL::CallbackType>(*messageerror_handler, realm);
    port->add_event_listener_without_options(HTML::EventNames::messageerror, DOM::IDLEventListener::create(realm, messageerror_callback));

    // FIXME: 6. Enable port’s port message queue.

    // 7. Let startAlgorithm be an algorithm that returns undefined.
    auto start_algorithm = GC::create_function(realm.heap(), []() -> WebIDL::ExceptionOr<JS::Value> {
        return JS::js_undefined();
    });

    // 8. Let writeAlgorithm be the following steps, taking a chunk argument:
    auto write_algorithm = GC::create_function(realm.heap(), [&vm, &realm, state, port](JS::Value chunk) {
        // 1. If backpressurePromise is undefined, set backpressurePromise to a promise resolved with undefined.
        if (!state->backpressure_promise)
            state->backpressure_promise = WebIDL::create_resolved_promise(realm, JS::js_undefined());

        // 2. Return the result of reacting to backpressurePromise with the following fulfillment steps:
        return WebIDL::react_to_promise(*state->backpressure_promise,
            GC::create_function(realm.heap(), [&vm, &realm, state, port, chunk](JS::Value) -> WebIDL::ExceptionOr<JS::Value> {
                // 1. Set backpressurePromise to a new promise.
                state->backpressure_promise = WebIDL::create_promise(realm);

                // 2. Let result be PackAndPostMessageHandlingError(port, "chunk", chunk).
                auto result = pack_and_post_message_handling_error(port, JS::PrimitiveString::create(vm, "chunk"sv), chunk);

                // 3. If result is an abrupt completion,
                if (result.is_exception()) {
                    // 1. Disentangle port.
                    port->disentangle();

                    // 2. Return a promise rejected with result.[[Value]].
                    return JS::Value { WebIDL::create_rejected_promise_from_exception(realm, result.release_error())->promise().ptr() };
                }

                // 4. Otherwise, return a promise resolved with undefined.
                return JS::Value { WebIDL::create_resolved_promise(realm, JS::js_undefined())->promise().ptr() };
            }),
            {});
    });

    // 9. Let closeAlgorithm be the folowing steps:
    auto close_algorithm = GC::create_function(realm.heap(), [&vm, &realm, &port]() {
        // 1. Perform ! PackAndPostMessage(port, "close", undefined).
        MUST(pack_and_post_message(port, JS::PrimitiveString::create(vm, "close"sv), JS::js_undefined()));

        // 2. Disentangle port.
        port->disentangle();

        // 3. Return a promise resolved with undefined.
        return WebIDL::create_resolved_promise(realm, JS::js_undefined());
    });

    // 10. Let abortAlgorithm be the following steps, taking a reason argument:
    auto abort_algorithm = GC::create_function(realm.heap(), [&vm, &realm, port](JS::Value reason) {
        // 1. Let result be PackAndPostMessageHandlingError(port, "error", reason).
        auto result = pack_and_post_message_handling_error(port, JS::PrimitiveString::create(vm, "error"sv), reason);

        // 2. Disentangle port.
        port->disentangle();

        // 3. If result is an abrupt completion, return a promise rejected with result.[[Value]].
        if (result.is_exception())
            return WebIDL::create_rejected_promise_from_exception(realm, result.release_error());

        // 4. Otherwise, return a promise resolved with undefined.
        return WebIDL::create_resolved_promise(realm, JS::js_undefined());
    });

    // 11. Let sizeAlgorithm be an algorithm that returns 1.
    auto size_algorithm = GC::create_function(realm.heap(), [](JS::Value) { return JS::normal_completion(JS::Value(1)); });

    // 12. Perform ! SetUpWritableStreamDefaultController(stream, controller, startAlgorithm, writeAlgorithm, closeAlgorithm, abortAlgorithm, 1, sizeAlgorithm).
    MUST(set_up_writable_stream_default_controller(*this, controller, start_algorithm, write_algorithm, close_algorithm, abort_algorithm, 1, size_algorithm));
}

WritableStream::WritableStream(JS::Realm& realm)
    : Bindings::PlatformObject(realm)
{
}

void WritableStream::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    WEB_SET_PROTOTYPE_FOR_INTERFACE(WritableStream);
}

void WritableStream::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_close_request);
    visitor.visit(m_controller);
    visitor.visit(m_in_flight_write_request);
    visitor.visit(m_in_flight_close_request);
    if (m_pending_abort_request.has_value()) {
        visitor.visit(m_pending_abort_request->promise);
        visitor.visit(m_pending_abort_request->reason);
    }
    visitor.visit(m_stored_error);
    visitor.visit(m_writer);
    for (auto& write_request : m_write_requests)
        visitor.visit(write_request);
}

}
