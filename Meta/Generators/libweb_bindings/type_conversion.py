# Copyright (c) 2026-present, the Ladybird developers.
#
# SPDX-License-Identifier: BSD-2-Clause

from Generators.libweb_bindings.context import GenerationContext
from Generators.libweb_bindings.includes import GeneratedIncludes
from Utils.utils import make_name_acceptable_cpp
from Utils.utils import title_casify
from Utils.utils import title_case_to_snake_case
from Utils.webidl_parser import CallbackFunction
from Utils.webidl_parser import DictionaryMember


def cpp_name(member: DictionaryMember) -> str:
    return make_name_acceptable_cpp(title_case_to_snake_case(member.name))


def is_optional_without_default(member: DictionaryMember) -> bool:
    return not member.required and member.default_value is None


def is_callback(member: DictionaryMember, context: GenerationContext) -> bool:
    return context.callback_function(member.type) is not None


def cpp_value_type(member: DictionaryMember, context: GenerationContext) -> str:
    if member.type == "any":
        return "JS::Value"
    if member.type == "boolean":
        return "bool"
    if member.type == "unrestricted double":
        return "double"
    if member.type == "unsigned long long":
        return "WebIDL::UnsignedLongLong"
    if is_callback(member, context):
        return "GC::Ptr<WebIDL::CallbackType>"
    return member.type


def cpp_type(member: DictionaryMember, context: GenerationContext) -> str:
    value_type = cpp_value_type(member, context)
    if is_optional_without_default(member) and not is_callback(member, context):
        return f"Optional<{value_type}>"
    return value_type


def cpp_empty_value(member: DictionaryMember, context: GenerationContext) -> str:
    if is_callback(member, context):
        return "nullptr"
    return "OptionalNone {}"


def add_header_includes_for_type(
    member: DictionaryMember,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> None:
    if is_optional_without_default(member) and not is_callback(member, context):
        includes.add("AK/Optional.h")
    if member.type == "any":
        includes.add("LibJS/Runtime/Value.h")
        return
    if member.type == "unsigned long long":
        includes.add("LibWeb/WebIDL/Types.h")
        return
    if is_callback(member, context):
        includes.add("LibGC/Ptr.h")
        includes.add("LibWeb/WebIDL/CallbackType.h")
        return
    if cpp_value_type(member, context) == member.type and not context.is_local_type(member.type):
        includes.add_binding(member.type)


def boolean_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibJS/Runtime/Value.h")

    # https://webidl.spec.whatwg.org/#js-boolean
    # 1. Let x be the result of computing ToBoolean(V).
    # 2. Return the IDL boolean value that is the one that represents the same truth value as the JavaScript Boolean value x.
    return f"{value_name}.to_boolean()"


def unrestricted_double_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibJS/Runtime/ValueInlines.h")

    # https://webidl.spec.whatwg.org/#js-unrestricted-float
    return f"{value_name}.to_double(vm)"


def any_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibJS/Runtime/Value.h")

    # https://webidl.spec.whatwg.org/#js-any
    # 1. If V is undefined, then return the unique undefined IDL value.
    # 2. If V is null, then return the null object? reference.
    # 3. If V is a Boolean, then return the boolean value that represents the same truth value.
    # 4. If V is a Number, then return the result of converting V to an unrestricted double.
    # 5. If V is a BigInt, then return the result of converting V to a bigint.
    # 6. If V is a String, then return the result of converting V to a DOMString.
    # 7. If V is a Symbol, then return the result of converting V to a symbol.
    # 8. If V is an Object, then return an IDL object value that references V.
    # NB: We're getting passed a JS::Value - which is already our C++ representation, so we can just return it as-is.
    return value_name


def unsigned_long_long_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibWeb/WebIDL/AbstractOperations.h")

    # https://webidl.spec.whatwg.org/#js-unsigned-long-long
    # 1. Let x be ? ConvertToInt(V, 64, "unsigned").
    # 2. Return the IDL unsigned long long value that represents the same numeric value as x.
    return f"WebIDL::convert_to_int<WebIDL::UnsignedLongLong>(vm, {value_name}, WebIDL::EnforceRange::Yes, WebIDL::Clamp::No)"


def callback_function_to_idl_value(
    callback_function: CallbackFunction,
    value_name: str,
    includes: GeneratedIncludes,
) -> str:
    includes.add("LibGC/Heap.h")
    includes.add("LibJS/Runtime/Error.h")
    includes.add("LibJS/Runtime/FunctionObject.h")
    includes.add("LibWeb/HTML/Scripting/Environments.h")
    includes.add("LibWeb/WebIDL/CallbackType.h")

    # https://webidl.spec.whatwg.org/#js-callback-function
    # 1. If the result of calling IsCallable(V) is false and the conversion to an IDL value is not being performed due to V being assigned to an attribute whose type is a nullable callback function that is annotated with [LegacyTreatNonObjectAsNull], then throw a TypeError.
    # 2. Return the IDL callback function type value that represents a reference to the same object that V represents, with the incumbent settings object as the callback context.
    operation_returns_promise = (
        "WebIDL::OperationReturnsPromise::Yes"
        if callback_function.return_type.split("<", 1)[0] == "Promise"
        else "WebIDL::OperationReturnsPromise::No"
    )

    legacy_treat_non_object_as_null = "LegacyTreatNonObjectAsNull" in callback_function.extended_attributes
    legacy_treat_non_object_as_null_conversion = ""
    if legacy_treat_non_object_as_null:
        legacy_treat_non_object_as_null_conversion = f"""                    if (!{value_name}.is_object())
                        return nullptr;
"""

    return f"""[&]() -> JS::ThrowCompletionOr<GC::Ptr<WebIDL::CallbackType>> {{
{legacy_treat_non_object_as_null_conversion}                    // 1. If the result of calling IsCallable(V) is false and the conversion to an IDL value is not being performed due to V being assigned to an attribute whose type is a nullable callback function that is annotated with [LegacyTreatNonObjectAsNull], then throw a TypeError.
                    if (!{value_name}.is_function())
                        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAFunction, {value_name});
                    return vm.heap().allocate<WebIDL::CallbackType>(
                        {value_name}.as_object(),
                        HTML::incumbent_realm(),
                        {operation_returns_promise});
                }}()"""


def to_idl_value(
    member: DictionaryMember,
    value_name: str,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> str:
    if member.type == "any":
        return any_to_idl_value(value_name, includes)
    if member.type == "boolean":
        return boolean_to_idl_value(value_name, includes)
    if member.type == "unrestricted double":
        return unrestricted_double_to_idl_value(value_name, includes)
    if member.type == "unsigned long long":
        return unsigned_long_long_to_idl_value(value_name, includes)
    callback_function = context.callback_function(member.type)
    if callback_function is not None:
        return callback_function_to_idl_value(callback_function, value_name, includes)
    converter_name = make_name_acceptable_cpp(title_case_to_snake_case(member.type))
    return f"convert_to_idl_value_for_{converter_name}(vm, {value_name})"


def cpp_default_value_conversion(
    member: DictionaryMember,
    context: GenerationContext,
) -> str:
    if member.default_value is None:
        raise RuntimeError(f"Dictionary member '{member.name}' has no default value")
    if member.type == "boolean":
        if member.default_value == "true":
            return "true"
        if member.default_value == "false":
            return "false"
    if member.default_value.startswith('"') and member.default_value.endswith('"'):
        default_value = title_casify(member.default_value.removeprefix('"').removesuffix('"'))
        return f"{cpp_type(member, context)}::{default_value}"
    raise RuntimeError(f"Unsupported default value for dictionary member '{member.name}'")
