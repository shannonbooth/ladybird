# Copyright (c) 2026-present, the Ladybird developers.
#
# SPDX-License-Identifier: BSD-2-Clause

from dataclasses import dataclass
from dataclasses import replace
from enum import Enum
from typing import Optional
from typing import TextIO
from typing import Union

from Generators.libweb_bindings.context import GenerationContext
from Generators.libweb_bindings.includes import GeneratedIncludes
from Utils.utils import make_name_acceptable_cpp
from Utils.utils import title_case_to_snake_case
from Utils.utils import title_casify
from Utils.webidl_parser import Attribute
from Utils.webidl_parser import CallbackFunction
from Utils.webidl_parser import DictionaryMember
from Utils.webidl_parser import IDLParameterizedType
from Utils.webidl_parser import IDLType
from Utils.webidl_parser import IDLUnionType
from Utils.webidl_parser import Interface
from Utils.webidl_parser import OperationParameter

DictionaryMemberOrAttribute = Union[DictionaryMember, Attribute, OperationParameter]


class ContainedStorageType(Enum):
    Vector = "Vector"
    ConservativeVector = "GC::ConservativeVector"
    RootVector = "GC::RootVector"


@dataclass
class CppType:
    name: str
    contained_storage_type: ContainedStorageType = ContainedStorageType.Vector
    is_nullable: bool = False
    is_optional_presence: bool = False
    gc_ref_target_type: str = ""


def cpp_name(member: DictionaryMember) -> str:
    return make_name_acceptable_cpp(title_case_to_snake_case(member.name))


def is_optional_without_default(member: DictionaryMemberOrAttribute) -> bool:
    return isinstance(member, DictionaryMember) and not member.required and member.default_value is None


def is_callback(member: DictionaryMemberOrAttribute, context: GenerationContext) -> bool:
    return context.callback_function(member.type) is not None


def is_numeric_type(type_name: str) -> bool:
    return type_name in (
        "byte",
        "octet",
        "short",
        "unsigned short",
        "long",
        "unsigned long",
        "long long",
        "unsigned long long",
        "float",
        "unrestricted float",
        "double",
        "unrestricted double",
    )


def is_string_type(type_name: str) -> bool:
    return type_name in ("DOMString", "USVString", "ByteString")


ARRAY_BUFFER_VIEW_TYPES = (
    "Int8Array",
    "Int16Array",
    "Int32Array",
    "Uint8Array",
    "Uint16Array",
    "Uint32Array",
    "Uint8ClampedArray",
    "BigInt64Array",
    "BigUint64Array",
    "Float16Array",
    "Float32Array",
    "Float64Array",
    "DataView",
)

BUFFER_SOURCE_TYPES = ("ArrayBuffer", *ARRAY_BUFFER_VIEW_TYPES)
TYPED_ARRAY_TYPES = tuple(type_name for type_name in ARRAY_BUFFER_VIEW_TYPES if type_name != "DataView")


def is_buffer_source_type(idl_type: IDLType) -> bool:
    return idl_type.name in BUFFER_SOURCE_TYPES


def is_typed_array_type(idl_type: IDLType) -> bool:
    return idl_type.name in TYPED_ARRAY_TYPES


def add_buffer_source_type_include(idl_type: IDLType, includes: GeneratedIncludes) -> None:
    if idl_type.name == "DataView":
        includes.add("LibJS/Runtime/DataView.h")
    elif idl_type.name == "ArrayBuffer":
        includes.add("LibJS/Runtime/ArrayBuffer.h")
    else:
        includes.add("LibJS/Runtime/TypedArray.h")


def builtin_type_to_union(idl_type: IDLType) -> Optional[IDLType]:
    if idl_type.name == "ArrayBufferView":
        return IDLUnionType([IDLType(name) for name in ARRAY_BUFFER_VIEW_TYPES], idl_type.nullable)
    if idl_type.name == "BufferSource":
        return IDLUnionType(
            [IDLType(name) for name in ARRAY_BUFFER_VIEW_TYPES] + [IDLType("ArrayBuffer")],
            idl_type.nullable,
        )
    return None


def resolve_type_for_conversion(idl_type: Union[IDLType, str], context: GenerationContext) -> IDLType:
    resolved_type = context.resolve_typedef(idl_type)

    if isinstance(resolved_type, IDLUnionType):
        return IDLUnionType(
            [resolve_type_for_conversion(member_type, context) for member_type in resolved_type.member_types],
            resolved_type.nullable,
        )

    if isinstance(resolved_type, IDLParameterizedType):
        return IDLParameterizedType(
            resolved_type.name,
            [resolve_type_for_conversion(parameter, context) for parameter in resolved_type.parameters],
            resolved_type.nullable,
        )

    builtin_type = builtin_type_to_union(resolved_type)
    if builtin_type is not None:
        return builtin_type

    return resolved_type


def contained_storage_type_to_cpp_name(contained_storage_type: ContainedStorageType) -> str:
    return contained_storage_type.value


def add_include_for_contained_storage_type(
    contained_storage_type: ContainedStorageType,
    includes: GeneratedIncludes,
) -> None:
    if contained_storage_type is ContainedStorageType.ConservativeVector:
        includes.add("LibGC/ConservativeVector.h")
    elif contained_storage_type is ContainedStorageType.RootVector:
        includes.add("LibGC/RootVector.h")
    else:
        includes.add("AK/Vector.h")


def make_cpp_type(
    name: str,
    contained_storage_type: ContainedStorageType = ContainedStorageType.Vector,
) -> CppType:
    return CppType(name=name, contained_storage_type=contained_storage_type)


def gc_ref_type(referent_type: str) -> CppType:
    return CppType(
        name=f"GC::Ref<{referent_type}>",
        contained_storage_type=ContainedStorageType.RootVector,
        gc_ref_target_type=referent_type,
    )


def gc_ptr_type(referent_type: str) -> CppType:
    return CppType(
        name=f"GC::Ptr<{referent_type}>",
        contained_storage_type=ContainedStorageType.RootVector,
        is_nullable=True,
        gc_ref_target_type=referent_type,
    )


def is_direct_gc_ref_cpp_type(cpp_type: CppType) -> bool:
    return bool(cpp_type.gc_ref_target_type) and not cpp_type.is_nullable


def type_contains_gc_like_value(context: GenerationContext, idl_type: IDLType) -> bool:
    if isinstance(idl_type, IDLUnionType):
        return any(type_contains_gc_like_value(context, member_type) for member_type in idl_type.member_types)

    if isinstance(idl_type, IDLParameterizedType):
        return any(type_contains_gc_like_value(context, parameter) for parameter in idl_type.parameters)

    return (
        context.interface(idl_type) is not None
        or is_buffer_source_type(idl_type)
        or context.callback_function(idl_type) is not None
        or idl_type.name in ("any", "object", "Promise")
    )


def contained_storage_type_for_aggregate_type(context: GenerationContext, idl_type: IDLType) -> ContainedStorageType:
    if type_contains_gc_like_value(context, idl_type):
        return ContainedStorageType.ConservativeVector
    return ContainedStorageType.Vector


def union_type_to_variant(union_type: IDLUnionType, context: GenerationContext) -> str:
    cpp_types = [
        cpp_type_for_idl_type(member_type.clone_with_nullable(False), context)
        for member_type in union_type.flattened_member_types()
        if member_type.name != "undefined"
    ]

    if union_type.includes_undefined() or union_type.includes_nullable_type():
        cpp_types.append("Empty")

    return f"Variant<{', '.join(cpp_types)}>"


def union_member_type_for_default_value(
    union_type: IDLUnionType,
    default_value: str,
    context: GenerationContext,
) -> IDLType:
    member_types = union_type.flattened_member_types()

    if default_value.startswith('"') and default_value.endswith('"'):
        string_type = next(
            (member_type for member_type in member_types if is_string_type(member_type.name)),
            None,
        )
        if string_type is not None:
            return string_type

    if default_value in ("true", "false"):
        boolean_type = next((member_type for member_type in member_types if member_type.name == "boolean"), None)
        if boolean_type is not None:
            return boolean_type

    if default_value in ("0", "0.0"):
        numeric_type = next((member_type for member_type in member_types if is_numeric_type(member_type.name)), None)
        if numeric_type is not None:
            return numeric_type

    if default_value == "null" and (union_type.includes_undefined() or union_type.includes_nullable_type()):
        return IDLType("undefined")

    raise RuntimeError(f"Unsupported union default value '{default_value}' for '{union_type}'")


def cpp_value_type(member: DictionaryMemberOrAttribute, context: GenerationContext) -> str:
    member_type = resolve_type_for_conversion(member.type, context)
    return cpp_type_for_idl_type(member_type, context, optional=is_optional_without_default(member))


def cpp_type_for_non_nullable_idl_type(idl_type: IDLType, context: GenerationContext) -> CppType:
    type_name = idl_type.name

    interface = context.interface(type_name)
    if interface is not None:
        return gc_ref_type(fully_qualified_name_for_interface(interface))

    if context.callback_function(type_name) is not None:
        return gc_ref_type("WebIDL::CallbackType")

    if is_buffer_source_type(idl_type):
        return gc_ref_type(f"JS::{type_name}")

    if type_name == "any":
        return make_cpp_type("JS::Value", ContainedStorageType.RootVector)
    if type_name == "boolean":
        return make_cpp_type("bool")
    if type_name in ("DOMString", "USVString"):
        return make_cpp_type("String")
    if type_name in ("double", "unrestricted double"):
        return make_cpp_type("double")
    if type_name in ("float", "unrestricted float"):
        return make_cpp_type("float")
    if type_name == "undefined":
        return make_cpp_type("Empty")
    if type_name == "object":
        return gc_ref_type("JS::Object")
    if type_name == "byte":
        return make_cpp_type("WebIDL::Byte")
    if type_name == "octet":
        return make_cpp_type("WebIDL::Octet")
    if type_name == "short":
        return make_cpp_type("WebIDL::Short")
    if type_name == "unsigned short":
        return make_cpp_type("WebIDL::UnsignedShort")
    if type_name == "long":
        return make_cpp_type("WebIDL::Long")
    if type_name == "unsigned long":
        return make_cpp_type("WebIDL::UnsignedLong")
    if type_name == "long long":
        return make_cpp_type("WebIDL::LongLong")
    if type_name == "unsigned long long":
        return make_cpp_type("WebIDL::UnsignedLongLong")

    if isinstance(idl_type, IDLParameterizedType):
        if type_name in ("sequence", "FrozenArray"):
            sequence_cpp_type = cpp_type_for_idl_type_details(idl_type.parameters[0], context)
            storage_type_name = contained_storage_type_to_cpp_name(sequence_cpp_type.contained_storage_type)
            return make_cpp_type(f"{storage_type_name}<{sequence_cpp_type.name}>")

    if isinstance(idl_type, IDLUnionType):
        cpp_type = make_cpp_type(
            union_type_to_variant(idl_type, context), contained_storage_type_for_aggregate_type(context, idl_type)
        )
        cpp_type.is_nullable = idl_type.includes_undefined() or idl_type.includes_nullable_type()
        return cpp_type

    return make_cpp_type(type_name)


def with_nullable_cpp_type(cpp_type: CppType) -> CppType:
    if cpp_type.name == "JS::Value":
        cpp_type.is_nullable = True
        return cpp_type

    if cpp_type.gc_ref_target_type:
        return gc_ptr_type(cpp_type.gc_ref_target_type)

    return CppType(
        name=f"Optional<{cpp_type.name}>",
        contained_storage_type=cpp_type.contained_storage_type,
        is_nullable=True,
        gc_ref_target_type=cpp_type.gc_ref_target_type,
    )


def with_optional_cpp_type(cpp_type: CppType) -> CppType:
    if is_direct_gc_ref_cpp_type(cpp_type):
        return CppType(
            name=f"GC::Ptr<{cpp_type.gc_ref_target_type}>",
            contained_storage_type=ContainedStorageType.RootVector,
            is_nullable=True,
            is_optional_presence=True,
            gc_ref_target_type=cpp_type.gc_ref_target_type,
        )

    return CppType(
        name=f"Optional<{cpp_type.name}>",
        contained_storage_type=ContainedStorageType.Vector,
        is_nullable=cpp_type.is_nullable,
        is_optional_presence=True,
        gc_ref_target_type=cpp_type.gc_ref_target_type,
    )


def cpp_type_for_idl_type_details(idl_type: IDLType, context: GenerationContext, optional: bool = False) -> CppType:
    resolved_type = resolve_type_for_conversion(idl_type, context)
    if not resolved_type.nullable or isinstance(resolved_type, IDLUnionType):
        cpp_type = cpp_type_for_non_nullable_idl_type(resolved_type, context)
    else:
        cpp_type = with_nullable_cpp_type(
            cpp_type_for_non_nullable_idl_type(resolved_type.clone_with_nullable(False), context)
        )

    if optional and not cpp_type.is_nullable:
        return with_optional_cpp_type(cpp_type)

    return cpp_type


def cpp_type_for_idl_type(idl_type: IDLType, context: GenerationContext, optional: bool = False) -> str:
    return cpp_type_for_idl_type_details(idl_type, context, optional).name


def cpp_type(member: DictionaryMemberOrAttribute, context: GenerationContext) -> str:
    value_type = cpp_value_type(member, context)
    if (
        is_optional_without_default(member)
        and not is_callback(member, context)
        and not value_type.startswith("Optional<")
    ):
        return f"Optional<{value_type}>"
    return value_type


def cpp_empty_value(member: DictionaryMember, context: GenerationContext) -> str:
    if is_callback(member, context):
        return "nullptr"
    return "OptionalNone {}"


def cpp_null_value(idl_type: IDLType, context: GenerationContext) -> str:
    if isinstance(resolve_type_for_conversion(idl_type, context), IDLUnionType):
        return "Empty {}"
    if cpp_type_for_idl_type_details(idl_type, context).gc_ref_target_type:
        return "nullptr"
    return "OptionalNone {}"


def operation_parameter_cpp_name(parameter: OperationParameter) -> str:
    return make_name_acceptable_cpp(parameter.name)


def operation_parameter_argument_name(parameter: OperationParameter) -> str:
    parameter_name = operation_parameter_cpp_name(parameter)
    if parameter.variadic:
        return f"move({parameter_name})"
    return parameter_name


def operation_parameter_cpp_type(parameter: OperationParameter, context: GenerationContext) -> str:
    if parameter.variadic:
        cpp_type = cpp_type_for_idl_type_details(parameter.type, context)
        storage_type_name = contained_storage_type_to_cpp_name(cpp_type.contained_storage_type)
        return f"{storage_type_name}<{cpp_type.name}>"

    if parameter.optional:
        return cpp_type_for_idl_type(parameter.type, context, optional=True)

    member = DictionaryMember(name=parameter.name, type=parameter.type, required=True)
    return cpp_value_type(member, context)


def operation_parameter_default_value(parameter: OperationParameter, context: GenerationContext) -> str:
    if parameter.default_value is None:
        raise RuntimeError(f"Operation parameter '{parameter.name}' has no default value")

    member = DictionaryMember(name=parameter.name, type=parameter.type, required=True)
    resolved_parameter_type = resolve_type_for_conversion(parameter.type, context)
    parameter_type = resolved_parameter_type.name
    if parameter.default_value == "{}":
        return f"{parameter_type} {{}}"
    if parameter.default_value == "null":
        cpp_value = cpp_value_type(member, context)
        null_value = cpp_null_value(resolved_parameter_type, context)
        return f"{cpp_value} {{ {null_value} }}"
    if parameter.default_value in ("true", "false"):
        return parameter.default_value
    if parameter.default_value == "0":
        integer_types = {
            "byte": "WebIDL::Byte",
            "octet": "WebIDL::Octet",
            "short": "WebIDL::Short",
            "unsigned short": "WebIDL::UnsignedShort",
            "long": "WebIDL::Long",
            "unsigned long": "WebIDL::UnsignedLong",
            "long long": "WebIDL::LongLong",
            "unsigned long long": "WebIDL::UnsignedLongLong",
        }
        if parameter_type in integer_types:
            return f"{integer_types[parameter_type]} {{ 0 }}"
        return "0"
    if parameter.default_value.startswith('"') and parameter.default_value.endswith('"'):
        return f"{parameter.default_value}_string"

    raise RuntimeError(f"Unsupported default value for operation parameter '{parameter.name}'")


def write_operation_parameter_conversions(
    out: TextIO,
    parameters: list[OperationParameter],
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> None:
    includes.add("LibWeb/Bindings/ExceptionOrUtils.h")

    for index, parameter in enumerate(parameters):
        if parameter.variadic:
            write_variadic_operation_parameter_conversion(out, parameter, index, includes, context)
        else:
            write_single_operation_parameter_conversion(out, parameter, index, includes, context)


def write_single_operation_parameter_conversion(
    out: TextIO,
    parameter: OperationParameter,
    index: int,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> None:
    argument_value_name = f"arg{index}"
    parameter_name = operation_parameter_cpp_name(parameter)
    conversion = to_idl_value(parameter, argument_value_name, includes, context)

    out.write(f"    auto {argument_value_name} = vm.argument({index});\n")

    if parameter.optional and parameter.default_value is None:
        out.write(
            f"""    {operation_parameter_cpp_type(parameter, context)} {parameter_name} {{}};
    if (!{argument_value_name}.is_undefined())
        {parameter_name} = TRY(throw_dom_exception_if_needed(vm, [&] {{ return {conversion}; }}));

"""
        )
        return

    if parameter.optional:
        out.write(
            f"""    auto {parameter_name} = {operation_parameter_default_value(parameter, context)};
    if (!{argument_value_name}.is_undefined())
        {parameter_name} = TRY(throw_dom_exception_if_needed(vm, [&] {{ return {conversion}; }}));

"""
        )
        return

    out.write(
        f"""    auto {parameter_name} = TRY(throw_dom_exception_if_needed(vm, [&] {{ return {conversion}; }}));

"""
    )


def write_variadic_operation_parameter_conversion(
    out: TextIO,
    parameter: OperationParameter,
    index: int,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> None:
    cpp_type = cpp_type_for_idl_type_details(parameter.type, context)
    add_include_for_contained_storage_type(cpp_type.contained_storage_type, includes)

    parameter_name = operation_parameter_cpp_name(parameter)
    argument_value_name = f"variadic_argument{index}"
    element_name = f"{parameter_name}_element"
    conversion = to_idl_value(parameter, argument_value_name, includes, context)
    parameter_cpp_type = operation_parameter_cpp_type(parameter, context)

    out.write(
        f"""    {parameter_cpp_type} {parameter_name};
    if (vm.argument_count() > {index}) {{
        {parameter_name}.ensure_capacity(vm.argument_count() - {index});
        for (size_t i = {index}; i < vm.argument_count(); ++i) {{
            auto {argument_value_name} = vm.argument(i);
            auto {element_name} = TRY(throw_dom_exception_if_needed(vm, [&] {{ return {conversion}; }}));
            {parameter_name}.unchecked_append(move({element_name}));
        }}
    }}

"""
    )


def add_header_includes_for_idl_type(
    idl_type: IDLType,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> None:
    idl_type = resolve_type_for_conversion(idl_type, context)

    if isinstance(idl_type, IDLUnionType):
        includes.add("AK/Variant.h")
        if idl_type.includes_undefined() or idl_type.includes_nullable_type():
            includes.add("AK/Types.h")
        for member_type in idl_type.flattened_member_types():
            add_header_includes_for_idl_type(member_type.clone_with_nullable(False), includes, context)
        return

    if isinstance(idl_type, IDLParameterizedType):
        if idl_type.name in ("sequence", "FrozenArray"):
            cpp_type = cpp_type_for_idl_type_details(idl_type, context)
            if cpp_type.name.startswith("GC::ConservativeVector"):
                includes.add("LibGC/ConservativeVector.h")
            elif cpp_type.name.startswith("GC::RootVector"):
                includes.add("LibGC/RootVector.h")
            else:
                includes.add("AK/Vector.h")
            for parameter in idl_type.parameters:
                add_header_includes_for_idl_type(parameter, includes, context)
            return

    type_name = idl_type.name
    if type_name == "undefined":
        includes.add("AK/Types.h")
        return
    if type_name == "any":
        includes.add("LibJS/Runtime/Value.h")
        return
    if type_name in ("unsigned long", "unsigned long long"):
        includes.add("LibWeb/WebIDL/Types.h")
        return
    if is_string_type(type_name):
        includes.add("AK/String.h")
        return

    interface = context.interface(idl_type)
    if interface is not None:
        includes.add("LibGC/Ptr.h")
        includes.add(implementation_header_for_interface(interface))
        return

    if context.callback_function(idl_type) is not None:
        includes.add("LibGC/Ptr.h")
        includes.add("LibWeb/WebIDL/CallbackType.h")
        return

    if is_buffer_source_type(idl_type):
        includes.add("LibGC/Ptr.h")
        add_buffer_source_type_include(idl_type, includes)
        return

    if cpp_type_for_idl_type(idl_type, context) == type_name and not context.is_local_type(idl_type):
        includes.add_binding(type_name)


def add_header_includes_for_type(
    member: DictionaryMember,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> None:
    if is_optional_without_default(member) and not is_callback(member, context):
        includes.add("AK/Optional.h")
    member_type = resolve_type_for_conversion(member.type, context)
    if "Optional<" in cpp_type(member, context):
        includes.add("AK/Optional.h")
    add_header_includes_for_idl_type(member_type, includes, context)


def implementation_header_for_interface(interface: Interface) -> str:
    path = interface.path.with_suffix(".h")
    parts = path.parts
    return f"LibWeb/{'/'.join(parts[parts.index('LibWeb') + 1 :])}"


def fully_qualified_name_for_interface(interface: Interface) -> str:
    parts = interface.path.parts
    namespace_name = parts[parts.index("LibWeb") + 1]
    return f"{namespace_name}::{interface.implemented_name}"


def converter_function_name_for_idl_type(idl_type: IDLType) -> str:
    converter_name = make_name_acceptable_cpp(title_case_to_snake_case(idl_type.name))
    return f"convert_to_idl_value_for_{converter_name}"


def unsupported_to_idl_value(idl_type: IDLType) -> str:
    raise RuntimeError(f"Unsupported IDL value conversion for '{idl_type}'")


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
    return f"WebIDL::convert_to_int<WebIDL::Byte>(vm, {value_name}, WebIDL::EnforceRange::No, WebIDL::Clamp::No)"


# 3.2.4.2. octet, https://webidl.spec.whatwg.org/#js-octet
def octet_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibWeb/WebIDL/Types.h")
    includes.add("LibWeb/WebIDL/AbstractOperations.h")

    # 1. Let x be ? ConvertToInt(V, 8, "unsigned").
    # 2. Return the IDL octet value that represents the same numeric value as x.
    return f"WebIDL::convert_to_int<WebIDL::Octet>(vm, {value_name}, WebIDL::EnforceRange::No, WebIDL::Clamp::No)"


# 3.2.4.3. short, https://webidl.spec.whatwg.org/#js-short
def short_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibWeb/WebIDL/Types.h")
    includes.add("LibWeb/WebIDL/AbstractOperations.h")

    # 1. Let x be ? ConvertToInt(V, 16, "signed").
    # 2. Return the IDL short value that represents the same numeric value as x.
    return f"WebIDL::convert_to_int<WebIDL::Short>(vm, {value_name}, WebIDL::EnforceRange::No, WebIDL::Clamp::No)"


# 3.2.4.4. unsigned short, https://webidl.spec.whatwg.org/#js-unsigned-short
def unsigned_short_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibWeb/WebIDL/Types.h")
    includes.add("LibWeb/WebIDL/AbstractOperations.h")

    # 1. Let x be ? ConvertToInt(V, 16, "unsigned").
    # 2. Return the IDL unsigned short value that represents the same numeric value as x.
    return (
        f"WebIDL::convert_to_int<WebIDL::UnsignedShort>(vm, {value_name}, WebIDL::EnforceRange::No, WebIDL::Clamp::No)"
    )


# 3.2.4.5. long, https://webidl.spec.whatwg.org/#js-long
def long_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibWeb/WebIDL/Types.h")
    includes.add("LibWeb/WebIDL/AbstractOperations.h")

    # 1. Let x be ? ConvertToInt(V, 32, "signed").
    # 2. Return the IDL long value that represents the same numeric value as x.
    return f"WebIDL::convert_to_int<WebIDL::Long>(vm, {value_name}, WebIDL::EnforceRange::No, WebIDL::Clamp::No)"


# 3.2.4.6. unsigned long, https://webidl.spec.whatwg.org/#js-unsigned-long
def unsigned_long_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibWeb/WebIDL/Types.h")
    includes.add("LibWeb/WebIDL/AbstractOperations.h")

    # 1. Let x be ? ConvertToInt(V, 32, "unsigned").
    # 2. Return the IDL unsigned long value that represents the same numeric value as x.
    return (
        f"WebIDL::convert_to_int<WebIDL::UnsignedLong>(vm, {value_name}, WebIDL::EnforceRange::No, WebIDL::Clamp::No)"
    )


# 3.2.4.7. long long, https://webidl.spec.whatwg.org/#js-long-long
def long_long_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibWeb/WebIDL/Types.h")
    includes.add("LibWeb/WebIDL/AbstractOperations.h")

    # 1. Let x be ? ConvertToInt(V, 64, "signed").
    # 2. Return the IDL long long value that represents the same numeric value as x.
    return f"WebIDL::convert_to_int<WebIDL::LongLong>(vm, {value_name}, WebIDL::EnforceRange::No, WebIDL::Clamp::No)"


# 3.2.4.8. unsigned long long, https://webidl.spec.whatwg.org/#js-unsigned-long-long
def unsigned_long_long_to_idl_value(value_name: str, includes: GeneratedIncludes) -> str:
    includes.add("LibWeb/WebIDL/Types.h")
    includes.add("LibWeb/WebIDL/AbstractOperations.h")

    # 1. Let x be ? ConvertToInt(V, 64, "unsigned").
    # 2. Return the IDL unsigned long long value that represents the same numeric value as x.
    return f"WebIDL::convert_to_int<WebIDL::UnsignedLongLong>(vm, {value_name}, WebIDL::EnforceRange::No, WebIDL::Clamp::No)"


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
def dom_string_to_idl_value(
    value_name: str,
    includes: GeneratedIncludes,
    extended_attributes: Optional[dict[str, str]] = None,
) -> str:
    includes.add("LibJS/Runtime/Value.h")
    includes.add("LibWeb/WebIDL/AbstractOperations.h")
    # 1. If V is null and the conversion is to an IDL type associated with the [LegacyNullToEmptyString] extended attribute, then return the DOMString value that represents the empty string.
    if extended_attributes is not None and "LegacyNullToEmptyString" in extended_attributes:
        return f"""[&]() -> JS::ThrowCompletionOr<String> {{
        if ({value_name}.is_null())
            return String {{}};
        return TRY(WebIDL::to_string(vm, {value_name}));
    }}()"""
    # 2. Let x be ? ToString(V).
    # 3. Return the IDL DOMString value that represents the same sequence of code units as the one the JavaScript String value x represents.
    return f"WebIDL::to_string(vm, {value_name})"


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
    includes.add("LibWeb/WebIDL/AbstractOperations.h")
    # 1. Let string be the result of converting V to a DOMString.
    # 2. If x contains any lone surrogates, then throw a TypeError.
    # 3. Return the IDL USVString value that represents the same sequence of code units as the one the JavaScript String value x represents.
    return f"WebIDL::to_usv_string(vm, {value_name})"


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


# 3.2.15. Interface types, https://webidl.spec.whatwg.org/#js-interface
def interface_to_idl_value(value_name: str, includes: GeneratedIncludes, interface: Interface) -> str:
    includes.add("LibJS/Runtime/Error.h")
    includes.add("LibJS/Runtime/Value.h")
    includes.add("LibJS/Runtime/ValueInlines.h")
    includes.add(implementation_header_for_interface(interface))

    # 1. If V implements I, then return the IDL interface type value that represents a reference to that platform object.
    # 2. Throw a TypeError.
    return f"""[&]() -> JS::ThrowCompletionOr<GC::Ref<{fully_qualified_name_for_interface(interface)}>> {{
        if (auto impl = {value_name}.as_if<{fully_qualified_name_for_interface(interface)}>())
            return *impl;
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "{interface.name}");
    }}()"""


# 3.2.16. Callback interface types, https://webidl.spec.whatwg.org/#js-callback-interface
def callback_interface_to_idl_value(
    value_name: str,
    includes: GeneratedIncludes,
    interface: Interface,
) -> str:
    # 1. If V is not an Object, then throw a TypeError.
    # 2. Return the IDL callback interface type value that represents a reference to V, with the incumbent settings object as the callback context.
    raise RuntimeError("callback interface to IDL value conversion is not yet implemented")


# 3.2.17. Dictionary types, https://webidl.spec.whatwg.org/#js-dictionary
def dictionary_to_idl_value(idl_type: IDLType, value_name: str) -> str:
    # The actual implementation of this conversion function is generated in idl.py.
    return f"{converter_function_name_for_idl_type(idl_type)}(vm, {value_name})"


# 3.2.18. Enumeration types, https://webidl.spec.whatwg.org/#js-enumeration
def enumeration_to_idl_value(idl_type: IDLType, value_name: str) -> str:
    # The actual implementation of this conversion function is generated in idl.py.
    return f"{converter_function_name_for_idl_type(idl_type)}(vm, {value_name})"


# 3.2.19. Callback function types, https://webidl.spec.whatwg.org/#js-callback-function
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


# 3.2.20. Nullable types — T?, https://webidl.spec.whatwg.org/#js-nullable-type
def nullable_to_idl_value(
    member: DictionaryMemberOrAttribute,
    value_name: str,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> str:
    member_type = resolve_type_for_conversion(member.type, context)
    inner_type = member_type.clone_with_nullable(False)
    inner_member = DictionaryMember(name=member.name, type=inner_type, required=True)
    inner_conversion = to_idl_value(inner_member, value_name, includes, context)
    null_value = cpp_null_value(member_type, context)
    return f"""[&]() -> JS::ThrowCompletionOr<{cpp_type(member, context)}> {{
        if ({value_name}.is_nullish())
            return {null_value};
        return TRY({inner_conversion});
    }}()"""


# 3.2.21. Sequences — sequence<T>, https://webidl.spec.whatwg.org/#js-sequence
def sequence_to_idl_value(
    member: DictionaryMemberOrAttribute,
    sequence_type: IDLParameterizedType,
    value_name: str,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> str:
    # 1. If V is not an Object, throw a TypeError.
    # 2. Let method be ? GetMethod(V, %Symbol.iterator%).
    # 3. If method is undefined, throw a TypeError.
    # 4. Return the result of creating a sequence from V and method.
    raise RuntimeError("sequence to IDL value conversion is not yet implemented")


# 3.2.22. Async sequences — async_sequence<T>, https://webidl.spec.whatwg.org/#js-async-iterable
def async_sequence_to_idl_value(
    member: DictionaryMemberOrAttribute,
    async_sequence_type: IDLParameterizedType,
    value_name: str,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> str:
    raise RuntimeError("async sequence to IDL value conversion is not yet implemented")


# 3.2.23. Records — record<K, V>, https://webidl.spec.whatwg.org/#js-record
def record_to_idl_value(
    member: DictionaryMemberOrAttribute,
    record_type: IDLParameterizedType,
    value_name: str,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> str:
    raise RuntimeError("record to IDL value conversion is not yet implemented")


# 3.2.24. Promise types — Promise<T>, https://webidl.spec.whatwg.org/#js-promise
def promise_to_idl_value(
    member: DictionaryMemberOrAttribute,
    promise_type: IDLParameterizedType,
    value_name: str,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> str:
    # 1. Let promiseCapability be ? NewPromiseCapability(%Promise%).
    # 2. Perform ? Call(promiseCapability.[[Resolve]], undefined, « V »).
    # 3. Return promiseCapability.
    raise RuntimeError("promise to IDL value conversion is not yet implemented")


# 3.2.25. Union types, https://webidl.spec.whatwg.org/#js-union
def union_to_idl_value(
    member: DictionaryMemberOrAttribute,
    union_type: IDLUnionType,
    value_name: str,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> str:
    # https://webidl.spec.whatwg.org/#es-union
    includes.add("AK/Variant.h")

    variant_type = union_type_to_variant(union_type, context)
    member_types = union_type.flattened_member_types()

    undefined_conversion = ""
    if union_type.includes_undefined():
        includes.add("AK/Types.h")
        undefined_conversion = f"""        // 1. If the union type includes undefined and V is undefined, then return the unique undefined value.
        if ({value_name}.is_undefined())
            return Empty {{}};

"""

    nullable_conversion = ""
    if union_type.includes_nullable_type():
        includes.add("AK/Types.h")
        nullable_conversion = f"""        // 2. If the union type includes a nullable type and V is null or undefined, then return the IDL value null.
        if ({value_name}.is_nullish())
            return Empty {{}};

"""

    object_conversions = ""
    platform_object_types = [member_type for member_type in member_types if context.interface(member_type) is not None]
    includes_object = any(member_type.name == "object" for member_type in member_types)
    array_buffer_type = next((member_type for member_type in member_types if member_type.name == "ArrayBuffer"), None)
    data_view_type = next((member_type for member_type in member_types if member_type.name == "DataView"), None)
    typed_array_types = [member_type for member_type in member_types if is_typed_array_type(member_type)]
    if (
        platform_object_types
        or includes_object
        or array_buffer_type is not None
        or data_view_type is not None
        or typed_array_types
    ):
        includes.add("AK/TypeCasts.h")
        object_conversions += f"""        if ({value_name}.is_object()) {{
            auto& object = {value_name}.as_object();
"""

        if platform_object_types:
            includes.add("LibWeb/Bindings/PlatformObject.h")
            object_conversions += """            // 5. If V is a platform object, then:
            if (is<PlatformObject>(object)) {
"""
            for platform_object_type in platform_object_types:
                interface = context.interface(platform_object_type)
                assert interface is not None
                platform_object_cpp_type = fully_qualified_name_for_interface(interface)
                includes.add(implementation_header_for_interface(interface))
                object_conversions += f"""                // 1. If types includes an interface type that V implements, then return the IDL value that is a reference to the object V.
                if (auto* result = as_if<{platform_object_cpp_type}>(object))
                    return {variant_type} {{ GC::Ref {{ *result }} }};
"""
            if includes_object:
                object_conversions += f"""                // 2. If types includes object, then return the IDL value that is a reference to the object V.
                return {variant_type} {{ GC::Ref {{ object }} }};
"""
            object_conversions += """            }
"""

        if array_buffer_type is not None:
            includes.add("LibJS/Runtime/ArrayBuffer.h")
            conversion_member = replace(member, type=array_buffer_type)
            array_buffer_conversion = to_idl_value(conversion_member, value_name, includes, context)
            object_conversions += f"""            // 6. If Type(V) is Object, V has an [[ArrayBufferData]] internal slot, and IsSharedArrayBuffer(V) is false, then:
            if (auto* array_buffer = as_if<JS::ArrayBuffer>(object)) {{
                if (!array_buffer->is_shared_array_buffer()) {{
                    // 1. If types includes ArrayBuffer, then return the result of converting V to ArrayBuffer.
                    auto array_buffer_union_type = TRY({array_buffer_conversion});
                    return {variant_type} {{ array_buffer_union_type }};
                }}
            }}
"""

        if data_view_type is not None:
            includes.add("LibJS/Runtime/DataView.h")
            conversion_member = replace(member, type=data_view_type)
            data_view_conversion = to_idl_value(conversion_member, value_name, includes, context)
            object_conversions += f"""            // 8. If Type(V) is Object and V has a [[DataView]] internal slot, then:
            if (as_if<JS::DataView>(object)) {{
                // 1. If types includes DataView, then return the result of converting V to DataView.
                auto data_view_union_type = TRY({data_view_conversion});
                return {variant_type} {{ data_view_union_type }};
            }}
"""

        if typed_array_types:
            includes.add("LibJS/Runtime/TypedArray.h")
            for typed_array_type in typed_array_types:
                conversion_member = replace(member, type=typed_array_type)
                typed_array_conversion = to_idl_value(conversion_member, value_name, includes, context)
                typed_array_cpp_name = make_name_acceptable_cpp(title_case_to_snake_case(typed_array_type.name))
                object_conversions += f"""            // 9. If Type(V) is Object and V has a [[TypedArrayName]] internal slot, then:
            if (as_if<JS::{typed_array_type.name}>(object)) {{
                // 1. If types includes a typed array type whose name is the value of V's [[TypedArrayName]] internal slot, then return the result of converting V to that type.
                auto {typed_array_cpp_name}_union_type = TRY({typed_array_conversion});
                return {variant_type} {{ {typed_array_cpp_name}_union_type }};
            }}
"""

        if includes_object:
            object_conversions += f"""            // 11.6. If types includes object, then return the IDL value that is a reference to the object V.
            return {variant_type} {{ GC::Ref {{ object }} }};
"""

        object_conversions += """        }

"""

    boolean_type = next((member_type for member_type in member_types if member_type.name == "boolean"), None)
    boolean_conversion = ""
    if boolean_type is not None:
        boolean_conversion = f"""        // 11. If Type(V) is Boolean, then:
        //     1. If types includes boolean, then return the result of converting V to boolean.
        if ({value_name}.is_boolean())
            return {variant_type} {{ {value_name}.as_bool() }};

"""

    numeric_type = next((member_type for member_type in member_types if is_numeric_type(member_type.name)), None)
    numeric_conversion = ""
    if numeric_type is not None:
        numeric_member = DictionaryMember(name=member.name, type=numeric_type, required=True)
        numeric_value_name = f"{make_name_acceptable_cpp(member.name)}_number"
        numeric_conversion_expression = to_idl_value(numeric_member, value_name, includes, context)
        numeric_conversion = f"""        // 12. If Type(V) is Number, then:
        //     1. If types includes a numeric type, then return the result of converting V to that numeric type.
        if ({value_name}.is_number()) {{
            auto {numeric_value_name} = TRY({numeric_conversion_expression});
            return {variant_type} {{ {numeric_value_name} }};
        }}

"""

    string_type = next((member_type for member_type in member_types if is_string_type(member_type.name)), None)
    string_conversion = ""
    if string_type is not None:
        string_member = DictionaryMember(name=member.name, type=string_type, required=True)
        string_value_name = f"{make_name_acceptable_cpp(member.name)}_string"
        string_conversion_expression = to_idl_value(string_member, value_name, includes, context)
        string_conversion = f"""        // 14. If types includes a string type, then return the result of converting V to that type.
        auto {string_value_name} = TRY({string_conversion_expression});
        return {variant_type} {{ {string_value_name} }};
"""

    throw_type_error = ""
    if not string_conversion:
        includes.add("LibJS/Runtime/Error.h")
        throw_type_error = """        // 19. Throw a TypeError.
        return vm.throw_completion<JS::TypeError>("No union types matched"sv);
"""

    return f"""[&]() -> JS::ThrowCompletionOr<{variant_type}> {{
{undefined_conversion}{nullable_conversion}{object_conversions}{boolean_conversion}{numeric_conversion}{string_conversion}{throw_type_error}    }}()"""


# 3.2.26. Buffer source types, https://webidl.spec.whatwg.org/#js-buffer-source-types
def buffer_source_to_idl_value(
    member: DictionaryMemberOrAttribute,
    value_name: str,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> str:
    member_type = resolve_type_for_conversion(member.type, context)
    type_name = member_type.name
    cpp_value = cpp_value_type(member, context)
    extended_attributes = getattr(member, "extended_attributes", {})

    includes.add("AK/TypeCasts.h")
    includes.add("LibJS/Runtime/Error.h")
    includes.add("LibJS/Runtime/Value.h")
    add_buffer_source_type_include(member_type, includes)

    def array_buffer_checks(buffer_name: str) -> str:
        allow_shared_check = ""
        if "AllowShared" not in extended_attributes:
            allow_shared_check = f"""        // 2. If IsSharedArrayBuffer(V) is true, then throw a TypeError.
        if ({buffer_name}->is_shared_array_buffer())
            return vm.throw_completion<JS::TypeError>(JS::ErrorType::SharedArrayBuffer);

"""

        allow_resizable_check = ""
        if "AllowResizable" not in extended_attributes:
            allow_resizable_check = f"""        // 3. If the conversion is not to an IDL type associated with the [AllowResizable] extended attribute, and
        //    IsFixedLengthArrayBuffer(V) is false, then throw a TypeError.
        if (!{buffer_name}->is_fixed_length())
            return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "fixed-length {type_name}");

"""

        return f"{allow_shared_check}{allow_resizable_check}"

    def array_buffer_view_checks(buffer_name: str) -> str:
        allow_shared_check = ""
        if "AllowShared" not in extended_attributes:
            allow_shared_check = f"""        // 2. If the conversion is not to an IDL type associated with the [AllowShared] extended attribute, and
        //    IsSharedArrayBuffer(V.[[ViewedArrayBuffer]]) is true, then throw a TypeError.
        if ({buffer_name}.is_shared_array_buffer())
            return vm.throw_completion<JS::TypeError>(JS::ErrorType::SharedArrayBuffer);

"""

        allow_resizable_check = ""
        if "AllowResizable" not in extended_attributes:
            allow_resizable_check = f"""        // 3. If the conversion is not to an IDL type associated with the [AllowResizable] extended attribute, and
        //    IsFixedLengthArrayBuffer(V.[[ViewedArrayBuffer]]) is false, then throw a TypeError.
        if (!{buffer_name}.is_fixed_length())
            return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "fixed-length {type_name}");

"""

        return f"{allow_shared_check}{allow_resizable_check}"

    if type_name == "ArrayBuffer":
        return f"""[&]() -> JS::ThrowCompletionOr<{cpp_value}> {{
        // A JavaScript value V is converted to an IDL ArrayBuffer value by running the following algorithm:
        // 1. If V is not an Object, or V does not have an [[ArrayBufferData]] internal slot, then throw a TypeError.
        auto builtin_buffer = {value_name}.as_if<JS::ArrayBuffer>();
        if (!builtin_buffer)
            return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "{type_name}");

{array_buffer_checks("builtin_buffer")}        // 4. Return the IDL ArrayBuffer value that is a reference to the same object as V.
        return GC::Ref {{ *builtin_buffer }};
    }}()"""

    if type_name == "DataView":
        includes.add("LibJS/Runtime/DataView.h")
        return f"""[&]() -> JS::ThrowCompletionOr<{cpp_value}> {{
        // A JavaScript value V is converted to an IDL DataView value by running the following algorithm:
        // 1. If V is not an Object, or V does not have a [[DataView]] internal slot, then throw a TypeError.
        auto builtin_buffer = {value_name}.as_if<JS::DataView>();
        if (!builtin_buffer)
            return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "{type_name}");

        auto& viewed_array_buffer = *builtin_buffer->viewed_array_buffer();

{array_buffer_view_checks("viewed_array_buffer")}        // 4. Return the IDL DataView value that is a reference to the same object as V.
        return GC::Ref {{ *builtin_buffer }};
    }}()"""

    if is_typed_array_type(member_type):
        includes.add("LibJS/Runtime/TypedArray.h")
        return f"""[&]() -> JS::ThrowCompletionOr<{cpp_value}> {{
        // A JavaScript value V is converted to an IDL typed array value by running the following algorithm:
        // 1. Let T be the IDL type V is being converted to.
        // 2. If V is not an Object, or V does not have a [[TypedArrayName]] internal slot with a value equal to T's name,
        //    then throw a TypeError.
        auto builtin_buffer = {value_name}.as_if<JS::{type_name}>();
        if (!builtin_buffer)
            return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "{type_name}");

        auto& viewed_array_buffer = *builtin_buffer->viewed_array_buffer();

{array_buffer_view_checks("viewed_array_buffer")}        // 5. Return the IDL value of type T that is a reference to the same object as V.
        return GC::Ref {{ *builtin_buffer }};
    }}()"""

    return unsupported_to_idl_value(member_type)


# 3.2.27. Frozen arrays — FrozenArray<T>, https://webidl.spec.whatwg.org/#js-frozen-array
def frozen_array_to_idl_value(
    member: DictionaryMemberOrAttribute,
    frozen_array_type: IDLParameterizedType,
    value_name: str,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> str:
    # 1. Let values be the result of converting V to IDL type sequence<T>.
    # 2. Return the result of creating a frozen array from values.
    raise RuntimeError("frozen array to IDL value conversion is not yet implemented")


def to_idl_value(
    member: DictionaryMemberOrAttribute,
    value_name: str,
    includes: GeneratedIncludes,
    context: GenerationContext,
) -> str:
    member_type = resolve_type_for_conversion(member.type, context)
    type_name = member_type.name

    if member_type.nullable:
        return nullable_to_idl_value(member, value_name, includes, context)

    if type_name == "any":
        return any_to_idl_value(value_name, includes)
    if type_name == "boolean":
        return boolean_to_idl_value(value_name, includes)
    if type_name == "byte":
        return byte_to_idl_value(value_name, includes)
    if type_name == "octet":
        return octet_to_idl_value(value_name, includes)
    if type_name == "short":
        return short_to_idl_value(value_name, includes)
    if type_name == "unsigned short":
        return unsigned_short_to_idl_value(value_name, includes)
    if type_name == "long":
        return long_to_idl_value(value_name, includes)
    if type_name == "unsigned long":
        return unsigned_long_to_idl_value(value_name, includes)
    if type_name == "long long":
        return long_long_to_idl_value(value_name, includes)
    if type_name == "unsigned long long":
        return unsigned_long_long_to_idl_value(value_name, includes)
    if type_name == "float":
        return float_to_idl_value(value_name, includes, member.name)
    if type_name == "unrestricted float":
        return unrestricted_float_to_idl_value(value_name, includes)
    if type_name == "double":
        return double_to_idl_value(value_name, includes)
    if type_name == "unrestricted double":
        return unrestricted_double_to_idl_value(value_name, includes)
    if type_name == "bigint":
        return bigint_to_idl_value(value_name, includes)
    if type_name == "DOMString":
        return dom_string_to_idl_value(
            value_name,
            includes,
            getattr(member, "extended_attributes", {}),
        )
    if type_name == "ByteString":
        return bytestring_to_idl_value(value_name, includes)
    if type_name == "USVString":
        return usv_string_to_idl_value(value_name, includes)
    if type_name == "object":
        return object_to_idl_value(value_name, includes)
    if type_name == "symbol":
        return symbol_to_idl_value(value_name, includes)

    interface = context.interface(member_type)
    if interface is not None and not interface.is_callback_interface:
        return interface_to_idl_value(value_name, includes, interface)

    if interface is not None and interface.is_callback_interface:
        return callback_interface_to_idl_value(value_name, includes, interface)

    if context.dictionary(member_type) is not None:
        return dictionary_to_idl_value(member_type, value_name)

    if context.enumeration(member_type) is not None:
        return enumeration_to_idl_value(member_type, value_name)

    callback_function = context.callback_function(member_type)
    if callback_function is not None:
        return callback_function_to_idl_value(callback_function, value_name, includes)

    if isinstance(member_type, IDLParameterizedType) and type_name == "sequence":
        return sequence_to_idl_value(member, member_type, value_name, includes, context)

    if isinstance(member_type, IDLParameterizedType) and type_name == "async_sequence":
        return async_sequence_to_idl_value(member, member_type, value_name, includes, context)

    if isinstance(member_type, IDLParameterizedType) and type_name == "record":
        return record_to_idl_value(member, member_type, value_name, includes, context)

    if isinstance(member_type, IDLParameterizedType) and type_name == "Promise":
        return promise_to_idl_value(member, member_type, value_name, includes, context)

    if isinstance(member_type, IDLUnionType):
        return union_to_idl_value(member, member_type, value_name, includes, context)

    if is_buffer_source_type(member_type):
        return buffer_source_to_idl_value(member, value_name, includes, context)

    if isinstance(member_type, IDLParameterizedType) and type_name == "FrozenArray":
        return frozen_array_to_idl_value(member, member_type, value_name, includes, context)

    return unsupported_to_idl_value(member_type)


# FIXME: Factor this in a way matching the specification.
def idl_value_to_javascript_value(
    idl_type: Union[IDLType, str],
    value: str,
    includes: GeneratedIncludes,
    context: GenerationContext,
    recursion_depth: int = 0,
) -> str:
    includes.add("LibJS/Runtime/Value.h")
    idl_type = resolve_type_for_conversion(idl_type, context)

    if isinstance(idl_type, IDLUnionType):
        includes.add("AK/Variant.h")
        conversions = []
        for index, member_type in enumerate(idl_type.flattened_member_types()):
            if member_type.name == "undefined":
                continue
            inner_type = member_type.clone_with_nullable(False)
            visited_value = f"visited_union_value{index}"
            visited_cpp_type = cpp_type_for_idl_type(inner_type, context)
            converted_value = idl_value_to_javascript_value(
                inner_type, visited_value, includes, context, recursion_depth + 1
            )
            conversions.append(
                f"""        [&]({visited_cpp_type} const& {visited_value}) -> JS::Value
        {{
            return {converted_value};
        }}"""
            )

        if idl_type.includes_nullable_type():
            conversions.append(
                """        [](Empty) -> JS::Value
        {
            return JS::js_null();
        }"""
            )
        elif idl_type.includes_undefined():
            conversions.append(
                """        [](Empty) -> JS::Value
        {
            return JS::js_undefined();
        }"""
            )

        return f"""{value}.visit(
{",\n".join(conversions)}
    )"""

    if idl_type.nullable:
        inner_type = idl_type.clone_with_nullable(False)
        value_is_nullable_pointer = (
            context.interface(inner_type) is not None or context.callback_function(inner_type) is not None
        )
        inner_value = value
        if not value_is_nullable_pointer:
            inner_value = f"{value}.value()"
        converted_value = idl_value_to_javascript_value(inner_type, inner_value, includes, context, recursion_depth + 1)
        has_value = value if value_is_nullable_pointer else f"{value}.has_value()"
        return f"""[&]() -> JS::Value {{
        if ({has_value})
            return JS::Value({converted_value});
        return JS::js_null();
    }}()"""

    idl_type_name = idl_type.name

    if isinstance(idl_type, IDLParameterizedType) and idl_type_name == "sequence":
        includes.add("LibJS/Runtime/Array.h")
        element_type = idl_type.parameters[0]
        length_name = f"sequence_length{recursion_depth}"
        array_name = f"sequence_array{recursion_depth}"
        index_name = f"sequence_index{recursion_depth}"
        element_name = f"sequence_element{recursion_depth}"
        js_element_name = f"js_sequence_element{recursion_depth}"
        converted_element = idl_value_to_javascript_value(
            element_type, element_name, includes, context, recursion_depth + 1
        )

        return f"""[&]() -> JS::Value {{
        // An IDL sequence<T> value S is converted to a JavaScript value as follows:
        // 1. Let n be the length of S.
        auto {length_name} = {value}.size();

        // 2. Let A be ! ArrayCreate(n).
        auto {array_name} = MUST(JS::Array::create(realm, {length_name}));

        // 3. Initialize i to be 0.
        // 4. For each element E of S, in order:
        for (size_t {index_name} = 0; {index_name} < {length_name}; ++{index_name}) {{
            auto& {element_name} = {value}.at({index_name});

            // 1. Let V be the result of converting E to a JavaScript value of the type T.
            JS::Value {js_element_name} = {converted_element};

            // 2. Perform ! CreateDataProperty(A, ! ToString(i), V).
            MUST({array_name}->create_data_property(JS::PropertyKey {{ {index_name} }}, {js_element_name}));

            // 3. Set i to i + 1.
        }}

        // 5. Return A.
        return {array_name};
    }}()"""

    # https://webidl.spec.whatwg.org/#js-type-mapping
    # The result of converting an IDL value to a JavaScript value depends on the IDL type of the value.
    if idl_type_name == "undefined":
        return "JS::js_undefined()"

    if idl_type_name == "any":
        return value

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

    if idl_type_name == "Promise":
        includes.add("AK/TypeCasts.h")
        includes.add("LibJS/Runtime/Promise.h")
        includes.add("LibWeb/WebIDL/Promise.h")
        return f"GC::Ref {{ as<JS::Promise>(*{value}->promise()) }}"

    interface = context.interface(idl_type_name)
    if interface is not None:
        includes.add(implementation_header_for_interface(interface))
        return f"&const_cast<{fully_qualified_name_for_interface(interface)}&>(*{value})"

    callback_function = context.callback_function(idl_type_name)
    if callback_function is not None:
        includes.add("LibWeb/WebIDL/CallbackType.h")
        return f"{value}->callback"

    raise RuntimeError(f"Unsupported IDL value conversion for '{idl_type_name}'")


def cpp_default_value_conversion(
    member: DictionaryMember,
    context: GenerationContext,
) -> str:
    if member.default_value is None:
        raise RuntimeError(f"Dictionary member '{member.name}' has no default value")

    member_type = resolve_type_for_conversion(member.type, context)
    if isinstance(member_type, IDLUnionType):
        if member.default_value == "null" and (
            member_type.includes_undefined() or member_type.includes_nullable_type()
        ):
            return "Empty {}"

        union_member_type = union_member_type_for_default_value(member_type, member.default_value, context)
        if union_member_type.name == "undefined":
            return "Empty {}"

        union_member = DictionaryMember(
            name=member.name,
            type=union_member_type,
            required=True,
            default_value=member.default_value,
        )
        expression = cpp_default_value_conversion(union_member, context)
        if expression == "{}":
            return f"{cpp_type_for_idl_type(union_member_type, context)} {{}}"
        return expression

    if member.default_value == "null":
        if member_type.name == "any":
            return "JS::js_null()"
        return cpp_null_value(member_type, context)
    if member.type == "boolean":
        if member.default_value == "true":
            return "true"
        if member.default_value == "false":
            return "false"
    if member.default_value.startswith('"') and member.default_value.endswith('"'):
        default_value = title_casify(member.default_value.removeprefix('"').removesuffix('"'))
        if resolve_type_for_conversion(member.type, context).name in ("DOMString", "USVString"):
            return f"{member.default_value}_string"
        return f"{cpp_type(member, context)}::{default_value}"
    raise RuntimeError(f"Unsupported default value for dictionary member '{member.name}'")
