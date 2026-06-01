# Copyright (c) 2026-present, the Ladybird developers.
#
# SPDX-License-Identifier: BSD-2-Clause

from Generators.libweb_bindings.context import GenerationContext
from Generators.libweb_bindings.context import type_name
from Generators.libweb_bindings.includes import GeneratedIncludes
from Utils.utils import make_name_acceptable_cpp
from Utils.utils import title_casify
from Utils.utils import title_case_to_snake_case
from Utils.webidl_parser import Attribute
from Utils.webidl_parser import CallbackFunction
from Utils.webidl_parser import DictionaryMember
from Utils.webidl_parser import IDLType
from Utils.webidl_parser import Interface


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
    return str(member.type)


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
    if cpp_value_type(member, context) == str(member.type) and not context.is_local_type(member.type):
        includes.add_binding(str(member.type))


def implementation_header_for_interface(interface: Interface) -> str:
    path = interface.path.with_suffix(".h")
    parts = path.parts
    return f"LibWeb/{'/'.join(parts[parts.index('LibWeb') + 1 :])}"


def fully_qualified_name_for_interface(interface: Interface) -> str:
    parts = interface.path.parts
    namespace_name = parts[parts.index("LibWeb") + 1]
    return f"{namespace_name}::{interface.implemented_name}"


# 3.2.1. any, https://webidl.spec.whatwg.org/#js-any
def any_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibJS/Runtime/Value.h")

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


# 3.2.3. boolean, https://webidl.spec.whatwg.org/#js-boolean
def boolean_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibJS/Runtime/Value.h")

    # 1. Let x be the result of computing ToBoolean(V).
    # 2. Return the IDL boolean value that is the one that represents the same truth value as the JavaScript Boolean value x.
    return f"{value_name}.to_boolean()"


# 3.2.4.1. byte, https://webidl.spec.whatwg.org/#js-byte
def byte_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibWeb/WebIDL/Types.h")
    includes.add("LibWeb/WebIDL/AbstractOperations.h")

    # 1. Let x be ? ConvertToInt(V, 8, "signed").
    # 2. Return the IDL byte value that represents the same numeric value as x.
    return f"WebIDL::convert_to_int<WebIDL::Byte>(vm, {value_name}, WebIDL::EnforceRange::Yes, WebIDL::Clamp::No)"


# 3.2.4.2. octet, https://webidl.spec.whatwg.org/#js-octet
def octet_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibWeb/WebIDL/Types.h")
    includes.add("LibWeb/WebIDL/AbstractOperations.h")

    # 1. Let x be ? ConvertToInt(V, 8, "unsigned").
    # 2. Return the IDL octet value that represents the same numeric value as x.
    return f"WebIDL::convert_to_int<WebIDL::Octet>(vm, {value_name}, WebIDL::EnforceRange::Yes, WebIDL::Clamp::No)"


# 3.2.4.3. short, https://webidl.spec.whatwg.org/#js-short
def short_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibWeb/WebIDL/Types.h")
    includes.add("LibWeb/WebIDL/AbstractOperations.h")

    # 1. Let x be ? ConvertToInt(V, 16, "signed").
    # 2. Return the IDL short value that represents the same numeric value as x.
    return f"WebIDL::convert_to_int<WebIDL::Short>(vm, {value_name}, WebIDL::EnforceRange::Yes, WebIDL::Clamp::No)"


# 3.2.4.4. unsigned short, https://webidl.spec.whatwg.org/#js-unsigned-short
def unsigned_short_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibWeb/WebIDL/Types.h")
    includes.add("LibWeb/WebIDL/AbstractOperations.h")

    # 1. Let x be ? ConvertToInt(V, 16, "unsigned").
    # 2. Return the IDL unsigned short value that represents the same numeric value as x.
    return f"WebIDL::convert_to_int<WebIDL::UnsignedShort>(vm, {value_name}, WebIDL::EnforceRange::Yes, WebIDL::Clamp::No)"


# 3.2.4.5. long, https://webidl.spec.whatwg.org/#js-long
def long_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibWeb/WebIDL/Types.h")
    includes.add("LibWeb/WebIDL/AbstractOperations.h")

    # 1. Let x be ? ConvertToInt(V, 32, "signed").
    # 2. Return the IDL long value that represents the same numeric value as x.
    return f"WebIDL::convert_to_int<WebIDL::Long>(vm, {value_name}, WebIDL::EnforceRange::Yes, WebIDL::Clamp::No)"


# 3.2.4.6. unsigned long, https://webidl.spec.whatwg.org/#js-unsigned-long
def unsigned_long_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibWeb/WebIDL/Types.h")
    includes.add("LibWeb/WebIDL/AbstractOperations.h")

    # 1. Let x be ? ConvertToInt(V, 32, "unsigned").
    # 2. Return the IDL unsigned long value that represents the same numeric value as x.
    return f"WebIDL::convert_to_int<WebIDL::UnsignedLong>(vm, {value_name}, WebIDL::EnforceRange::Yes, WebIDL::Clamp::No)"


# 3.2.4.7. long long, https://webidl.spec.whatwg.org/#js-long-long
def long_long_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibWeb/WebIDL/Types.h")
    includes.add("LibWeb/WebIDL/AbstractOperations.h")

    # 1. Let x be ? ConvertToInt(V, 64, "signed").
    # 2. Return the IDL long long value that represents the same numeric value as x.
    return f"WebIDL::convert_to_int<WebIDL::LongLong>(vm, {value_name}, WebIDL::EnforceRange::Yes, WebIDL::Clamp::No)"


# 3.2.4.8. unsigned long long, https://webidl.spec.whatwg.org/#js-unsigned-long-long
def unsigned_long_long_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibWeb/WebIDL/Types.h")
    includes.add("LibWeb/WebIDL/AbstractOperations.h")

    # 1. Let x be ? ConvertToInt(V, 64, "unsigned").
    # 2. Return the IDL unsigned long long value that represents the same numeric value as x.
    return f"WebIDL::convert_to_int<WebIDL::UnsignedLongLong>(vm, {value_name}, WebIDL::EnforceRange::Yes, WebIDL::Clamp::No)"


# 3.2.5. float, https://webidl.spec.whatwg.org/#js-float
def float_to_idl_value(value_name: str, includes: GeneratedIncludes, identifier: str) -> str:
    includes.add("LibJS/Runtime/Error.h")
    includes.add("LibJS/Runtime/ValueInlines.h")

    # 1. Let x be ? ToNumber(V).
    # 2. If x is NaN, +∞, or −∞, then throw a TypeError.
    # 3. Let S be the set of finite IEEE 754 single-precision floating point values except −0, but with two special values added: 2^128 and −2^128.
    # 4. Let y be the number in S that is closest to x, selecting the number with an even significand if there are two equally close values. (The two special values 2^128 and −2^128 are considered to have even significands for this purpose.)
    # 5. If y is 2^128 or −2^128, then throw a TypeError.
    # 6. If y is +0 and x is negative, return −0.
    # 7. Return y.
    # FIXME: Correctly implement steps 3-7.
    return f"""[&]() -> JS::ThrowCompletionOr<float> {{
        float x = TRY({value_name}.to_double(vm));
        if (isinf(x) || isnan(x))
            return vm.throw_completion<JS::TypeError>(JS::ErrorType::InvalidRestrictedFloatingPointParameter, "{identifier}");
        return x;
    }}()"""


# 3.2.6. unrestricted float, https://webidl.spec.whatwg.org/#js-unrestricted-float
def unrestricted_float_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    # 1. Let x be ? ToNumber(V).
    # 2. If x is NaN, then return the IDL unrestricted float value that represents the IEEE 754 NaN value with the bit pattern 0x7fc00000 [IEEE-754].
    # 3. Let S be the set of finite IEEE 754 single-precision floating point values except −0, but with two special values added: 2^128 and −2^128.
    # 4. Let y be the number in S that is closest to x, selecting the number with an even significand if there are two equally close values. (The two special values 2^128 and −2^128 are considered to have even significands for this purpose.)
    # 5. If y is 2^128, return +∞.
    # 6. If y is −2^128, return −∞.
    # 7. If y is +0 and x is negative, return −0.
    # 8. Return y.
    # FIXME.
    raise RuntimeError("unrestricted float to IDL value conversion is not yet implemented")
    return f"{value_name}.to_double(vm)"


# 3.2.7. double, https://webidl.spec.whatwg.org/#js-double
def double_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    # 1. Let x be ? ToNumber(V).
    # 2. If x is NaN, +∞, or −∞, then throw a TypeError.
    # 3. Return the IDL double value that represents the same numeric value as x.
    # FIXME.
    raise RuntimeError("double to IDL value conversion is not yet implemented")
    return f"{value_name}.to_double(vm)"


# 3.2.8. unrestricted double, https://webidl.spec.whatwg.org/#js-unrestricted-double
def unrestricted_double_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibJS/Runtime/ValueInlines.h")
    # 1. Let x be ? ToNumber(V).
    # 2. If x is NaN, then return the IDL unrestricted double value that represents the IEEE 754 NaN value with the bit pattern 0x7ff8000000000000 [IEEE-754].
    # 3. Return the IDL unrestricted double value that represents the same numeric value as x.
    # FIXME!
    return f"{value_name}.to_double(vm)"


# 3.2.9. bigint, https://webidl.spec.whatwg.org/#js-bigint
def bigint_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibJS/Runtime/Value.h")

    # 1. Let x be ? ToBigInt(V).
    # 2. Return the IDL bigint value that represents the same numeric value as x.
    # FIXME.
    raise RuntimeError("bigint to IDL value conversion is not yet implemented")


# 3.2.10. DOMString, https://webidl.spec.whatwg.org/#js-domstring
def dom_string_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibJS/Runtime/Value.h")
    # 1. If V is null and the conversion is to an IDL type associated with the [LegacyNullToEmptyString] extended attribute, then return the DOMString value that represents the empty string.
    # 2. Let x be ? ToString(V).
    # 3. Return the IDL DOMString value that represents the same sequence of code units as the one the JavaScript String value x represents.
    raise RuntimeError("DOMString to IDL value conversion is not yet implemented")


# 3.2.11. ByteString, https://webidl.spec.whatwg.org/#js-bytestring
def bytestring_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibJS/Runtime/Value.h")
    # 1. Let x be ? ToString(V).
    # 2. If the value of any element of x is greater than 255, then throw a TypeError.
    # 3. Return an IDL ByteString value whose length is the length of x, and where the value of each element is the value of the corresponding element of x.
    raise RuntimeError("ByteString to IDL value conversion is not yet implemented")


# 3.2.12. USVString, https://webidl.spec.whatwg.org/#js-usvstring
def usv_string_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibJS/Runtime/Value.h")
    # 1. Let string be the result of converting V to a DOMString.
    # 2. If x contains any lone surrogates, then throw a TypeError.
    # 3. Return the IDL USVString value that represents the same sequence of code units as the one the JavaScript String value x represents.
    raise RuntimeError("USVString to IDL value conversion is not yet implemented")


# 3.2.13. object, https://webidl.spec.whatwg.org/#js-object
def object_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibJS/Runtime/Value.h")
    # 1. If V is null, then return the null object? reference.
    # 2. If V is not an Object, then throw a TypeError.
    # 3. Return an IDL object value that references the same object that V represents.
    raise RuntimeError("object to IDL value conversion is not yet implemented")


# 3.2.14. symbol, https://webidl.spec.whatwg.org/#js-symbol
def symbol_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibJS/Runtime/Value.h")
    # 1. If V is not a Symbol, then throw a TypeError.
    # 2. Return the result of converting V to an IDL symbol value.
    raise RuntimeError("symbol to IDL value conversion is not yet implemented")


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
        if callback_function.return_type.name.split("<", 1)[0] == "Promise"
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


# FIXME: Factor this in a way matching the specification.
def idl_value_to_javascript_value(
    idl_type: IDLType | str,
    value: str,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> str:
    includes.add("LibJS/Runtime/Value.h")
    idl_type = context.resolve_typedef(idl_type)

    if idl_type.nullable:
        inner_type = idl_type.clone_with_nullable(False)
        inner_value = value
        if context.interface(inner_type) is None:
            inner_value = f"{value}.value()"
        converted_value = idl_value_to_javascript_value(inner_type, inner_value, includes, context)
        has_value = value if context.interface(inner_type) is not None else f"{value}.has_value()"
        return f"""[&]() -> JS::Value {{
        if ({has_value})
            return JS::Value({converted_value});
        return JS::js_null();
    }}()"""

    idl_type_name = idl_type.name

    # https://webidl.spec.whatwg.org/#js-type-mapping
    # The result of converting an IDL value to a JavaScript value depends on the IDL type of the value.
    if idl_type_name == "boolean":
        return f"JS::Value({value})"

    integer_type_map = {
        "byte": "WebIDL::Byte",
        "octet": "WebIDL::Octet",
        "short": "WebIDL::Short",
        "unsigned short": "WebIDL::UnsignedShort",
        "long": "WebIDL::Long",
        "unsigned long": "WebIDL::UnsignedLong",
        "long long": "double",
        "unsigned long long": "double",
    }
    if idl_type_name in integer_type_map:
        includes.add("LibWeb/WebIDL/Types.h")
        return f"JS::Value(static_cast<{integer_type_map[idl_type_name]}>({value}))"

    if idl_type_name in ("float", "unrestricted float", "double", "unrestricted double"):
        return f"JS::Value({value})"

    if idl_type_name in ("DOMString", "ByteString", "USVString"):
        includes.add("LibJS/Runtime/PrimitiveString.h")
        return f"JS::PrimitiveString::create(vm, {value})"

    interface = context.interface(idl_type_name)
    if interface is not None:
        includes.add(implementation_header_for_interface(interface))
        return f"&const_cast<{fully_qualified_name_for_interface(interface)}&>(*{value})"

    raise RuntimeError(f"Unsupported IDL value conversion for '{idl_type_name}'")


def to_idl_value(
    member: DictionaryMember | Attribute,
    value_name: str,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> str:
    if member.type == "any":
        return any_to_idl_value(value_name, includes)
    if member.type == "boolean":
        return boolean_to_idl_value(value_name, includes)
    if member.type == "byte":
        return byte_to_idl_value(value_name, includes)
    if member.type == "octet":
        return octet_to_idl_value(value_name, includes)
    if member.type == "short":
        return short_to_idl_value(value_name, includes)
    if member.type == "unsigned short":
        return unsigned_short_to_idl_value(value_name, includes)
    if member.type == "long":
        return long_to_idl_value(value_name, includes)
    if member.type == "unsigned long":
        return unsigned_long_to_idl_value(value_name, includes)
    if member.type == "long long":
        return long_long_to_idl_value(value_name, includes)
    if member.type == "unsigned long long":
        return unsigned_long_long_to_idl_value(value_name, includes)
    if member.type == "float":
        return float_to_idl_value(value_name, includes, member.name)
    if member.type == "unrestricted float":
        return unrestricted_float_to_idl_value(value_name, includes)
    if member.type == "double":
        return double_to_idl_value(value_name, includes)
    if member.type == "unrestricted double":
        return unrestricted_double_to_idl_value(value_name, includes)
    if member.type == "bigint":
        return bigint_to_idl_value(value_name, includes)
    if member.type == "DOMString":
        return dom_string_to_idl_value(value_name, includes)
    if member.type == "ByteString":
        return bytestring_to_idl_value(value_name, includes)
    if member.type == "USVString":
        return usv_string_to_idl_value(value_name, includes)
    if member.type == "object":
        return object_to_idl_value(value_name, includes)
    if member.type == "symbol":
        return symbol_to_idl_value(value_name, includes)
    callback_function = context.callback_function(member.type)
    if callback_function is not None:
        return callback_function_to_idl_value(callback_function, value_name, includes)
    converter_name = make_name_acceptable_cpp(title_case_to_snake_case(type_name(member.type)))
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
