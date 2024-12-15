/*
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/NativeFunction.h>
#include <LibJS/Runtime/PrimitiveString.h>
#include <LibJS/Runtime/VM.h>
#include <LibWeb/Bindings/ExceptionOrUtils.h>
#include <LibWeb/DOM/IDLEventListener.h>
#include <LibWeb/HTML/EventNames.h>
#include <LibWeb/HTML/MessagePort.h>
#include <LibWeb/HTML/StructuredSerializeOptions.h>
#include <LibWeb/Streams/AbstractOperations.h>
#include <LibWeb/Streams/ReadableStream.h>
#include <LibWeb/Streams/ReadableStreamDefaultController.h>
#include <LibWeb/Streams/Transferable.h>
#include <LibWeb/Streams/WritableStream.h>
#include <LibWeb/Streams/WritableStreamDefaultController.h>

namespace Web::Streams {

// https://streams.spec.whatwg.org/#abstract-opdef-crossrealmtransformsenderror
void cross_realm_transform_send_error(GC::Ref<HTML::MessagePort> port, JS::Value error)
{
    // 1. Perform PackAndPostMessage(port, "error", error), discarding the result.
    (void)pack_and_post_message(port, JS::PrimitiveString::create(port->vm(), "error"sv), error);
}

// https://streams.spec.whatwg.org/#abstract-opdef-packandpostmessage
WebIDL::ExceptionOr<void> pack_and_post_message(GC::Ref<HTML::MessagePort> port, GC::Ref<JS::PrimitiveString> type, JS::Value value)
{
    auto& vm = port->vm();
    auto& realm = port->realm();

    // 1. Let message be OrdinaryObjectCreate(null).
    auto message = JS::Object::create(realm, nullptr);

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

// https://streams.spec.whatwg.org/#abstract-opdef-packandpostmessagehandlingerror
WebIDL::ExceptionOr<void> pack_and_post_message_handling_error(GC::Ref<HTML::MessagePort> port, GC::Ref<JS::PrimitiveString> type, JS::Value value)
{
    // 1. Let result be PackAndPostMessage(port, type, value).
    auto result = pack_and_post_message(port, type, value);

    // 2. If result is an abrupt completion,
    if (result.is_exception()) {
        // 1. Perform ! CrossRealmTransformSendError(port, result.[[Value]]).
        cross_realm_transform_send_error(port, Bindings::exception_to_throw_completion(port->vm(), result.release_error()).release_value().value());
    }

    // 3. Return result as a completion record.
    return result;
}

// https://streams.spec.whatwg.org/#abstract-opdef-setupcrossrealmtransformreadable
void set_up_cross_realm_transform_readable(GC::Ref<ReadableStream> stream, GC::Ref<HTML::MessagePort> port)
{
    auto& vm = stream->vm();
    auto& realm = stream->realm();

    // 1. Perform ! InitializeReadableStream(stream).
    initialize_readable_stream(stream);

    // 2. Let controller be a new ReadableStreamDefaultController.
    auto controller = realm.create<ReadableStreamDefaultController>(realm);

    // 3. Add a handler for port’s message event with the following steps:
    auto message_handler = JS::NativeFunction::create(realm, [controller, port](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        // 1. Let data be the data of the message.
        auto message_event = vm.argument(0);
        auto data = MUST(message_event.get(vm, "data"));

        // 2. Assert: data is an Object.
        VERIFY(data.is_object());

        // 3. Let type be ! Get(data, "type").
        auto type_value = MUST(data.get(vm, vm.names.type));

        // 4. Let value be ! Get(data, "value").
        auto value = MUST(data.get(vm, vm.names.value));

        // 5. Assert: type is a String.
        VERIFY(type_value.is_string());
        auto type = type_value.as_string().utf8_string_view();

        // 6. If type is "chunk",
        if (type == "chunk"sv) {
            // 1. Perform ! ReadableStreamDefaultControllerEnqueue(controller, value).
            MUST(readable_stream_default_controller_enqueue(controller, value));
        }
        // 7. Otherwise, if type is "close",
        else if (type == "close"sv) {
            // 1. Perform ! ReadableStreamDefaultControllerClose(controller).
            readable_stream_default_controller_close(controller);

            // 2. Disentangle port.
            port->disentangle();
        }
        // 8. Otherwise, if type is "error",
        else if (type == "error"sv) {
            // 1. Perform ! ReadableStreamDefaultControllerError(controller, value).
            readable_stream_default_controller_error(controller, value);

            // 2. Disentangle port.
            port->disentangle();
        }

        return JS::js_undefined(); }, 1, "", &realm);
    auto message_callback = realm.heap().allocate<WebIDL::CallbackType>(*message_handler, realm);
    port->add_event_listener_without_options(HTML::EventNames::message, DOM::IDLEventListener::create(realm, message_callback));

    // 4. Add a handler for port’s messageerror event with the following steps:
    auto messageerror_handler = JS::NativeFunction::create(realm, [&realm, controller, port](JS::VM&) -> JS::ThrowCompletionOr<JS::Value> {
        // 1. Let error be a new "DataCloneError" DOMException.
        auto error = WebIDL::DataCloneError::create(realm, "Error transferring stream"_string);

        // 2. Perform ! CrossRealmTransformSendError(port, error).
        cross_realm_transform_send_error(port, error);

        // 3. Perform ! ReadableStreamDefaultControllerError(controller, error).
        readable_stream_default_controller_error(controller, error);

        // 4. Disentangle port.
        port->disentangle();

	return JS::js_undefined(); }, 0, "", &realm);
    auto messageerror_callback = realm.heap().allocate<WebIDL::CallbackType>(*messageerror_handler, realm);
    port->add_event_listener_without_options(HTML::EventNames::messageerror, DOM::IDLEventListener::create(realm, messageerror_callback));

    // 5. Enable port’s port message queue.
    port->enable_port_message_queue();

    // 6. Let startAlgorithm be an algorithm that returns undefined.
    auto start_algorithm = GC::create_function(realm.heap(), []() -> WebIDL::ExceptionOr<JS::Value> {
        return JS::js_undefined();
    });

    // 7. Let pullAlgorithm be the following steps:
    auto pull_algorithm = GC::create_function(realm.heap(), [&vm, &realm, port]() {
        // 1. Perform ! PackAndPostMessage(port, "pull", undefined).
        MUST(pack_and_post_message(port, JS::PrimitiveString::create(vm, "pull"sv), JS::js_undefined()));

        // 2. Return a promise resolved with undefined.
        return WebIDL::create_resolved_promise(realm, JS::js_undefined());
    });

    // 8. Let cancelAlgorithm be the following steps, taking a reason argument:
    auto cancel_algorithm = GC::create_function(realm.heap(), [&vm, &realm, port](JS::Value reason) {
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

    // 9. Let sizeAlgorithm be an algorithm that returns 1.
    auto size_algorithm = GC::create_function(realm.heap(), [](JS::Value) { return JS::normal_completion(JS::Value(1)); });

    // 10. Perform ! SetUpReadableStreamDefaultController(stream, controller, startAlgorithm, pullAlgorithm, cancelAlgorithm, 0, sizeAlgorithm).
    MUST(set_up_readable_stream_default_controller(stream, controller, start_algorithm, pull_algorithm, cancel_algorithm, 0, size_algorithm));
}

struct TransformForWritableAlgorithmState : GC::Cell {
    GC_CELL(UpdateAlgorithmState, JS::Cell);

    GC::Ptr<WebIDL::Promise> backpressure_promise;

private:
    explicit TransformForWritableAlgorithmState(GC::Ref<WebIDL::Promise> promise)
        : backpressure_promise(promise)
    {
    }

    virtual void visit_edges(JS::Cell::Visitor& visitor) override
    {
        GC::Cell::visit_edges(visitor);
        visitor.visit(backpressure_promise);
    }
};

// https://streams.spec.whatwg.org/#abstract-opdef-setupcrossrealmtransformwritable
void set_up_cross_realm_transform_writable(GC::Ref<WritableStream> stream, GC::Ref<HTML::MessagePort> port)
{
    auto& vm = stream->vm();
    auto& realm = stream->realm();

    // 1. Perform ! InitializeWritableStream(stream).
    initialize_writable_stream(stream);

    // 2. Let controller be a new WritableStreamDefaultController.
    auto controller = realm.create<WritableStreamDefaultController>(realm);

    // 3. Let backpressurePromise be a new promise.
    auto state = realm.heap().allocate<TransformForWritableAlgorithmState>(WebIDL::create_promise(realm));

    // 4. Add a handler for port’s message event with the following steps:
    auto message_handler = JS::NativeFunction::create(realm, [&realm, controller, state](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        // 1. Let data be the data of the message.
        auto message_event = vm.argument(0);
        auto data = MUST(message_event.get(vm, "data"));

        // 2. Assert: data is an Object.
        VERIFY(data.is_object());

        // 3. Let type be ! Get(data, "type").
        auto type_value = MUST(data.get(vm, vm.names.type));

        // 4. Let value be ! Get(data, "value").
        auto value = MUST(data.get(vm, vm.names.value));

        // 5. Assert: type is a String.
        VERIFY(type_value.is_string());
        auto type = type_value.as_string().utf8_string_view();

        // 6. If type is "pull",
        if (type == "pull"sv) {
            // 1. If backpressurePromise is not undefined,
            if (state->backpressure_promise) {
                // 1. Resolve backpressurePromise with undefined.
                WebIDL::resolve_promise(realm, *state->backpressure_promise, JS::js_undefined());

                // 2. Set backpressurePromise to undefined.
                state->backpressure_promise = nullptr;
            }
        }
        // 7. Otherwise, if type is "error",
        else if (type == "error"sv) {
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

        return JS::js_undefined(); }, 1, "", &realm);
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

    // 6. Enable port’s port message queue.
    port->enable_port_message_queue();

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
    auto close_algorithm = GC::create_function(realm.heap(), [&vm, &realm, port]() {
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
    MUST(set_up_writable_stream_default_controller(stream, controller, start_algorithm, write_algorithm, close_algorithm, abort_algorithm, 1, size_algorithm));
}

}
