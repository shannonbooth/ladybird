# Copyright (c) 2026-present, the Ladybird developers.
#
# SPDX-License-Identifier: BSD-2-Clause

from pathlib import Path
from typing import TextIO

from Generators.libweb_bindings import type_conversion
from Generators.libweb_bindings.context import GenerationContext
from Generators.libweb_bindings.includes import GeneratedIncludes
from Utils.utils import make_name_acceptable_cpp
from Utils.utils import title_case_to_snake_case
from Utils.webidl_parser import Attribute
from Utils.webidl_parser import Interface


def write_declaration(out: TextIO, includes: GeneratedIncludes, context: GenerationContext) -> None:
    interface = context.module.interface
    if interface is None:
        return

    ensure_interface_is_supported(interface)

    includes.add("LibJS/Runtime/NativeFunction.h")
    includes.add("LibJS/Runtime/Object.h")
    includes.add("LibWeb/Bindings/InterfaceObject.h")

    out.write(
        f"""struct {interface.constructor_class} {{
public:
    static void initialize(JS::Realm&, JS::NativeFunction&);
    static JS::ThrowCompletionOr<GC::Ref<JS::Object>> construct(InterfaceConstructor&, JS::FunctionObject&);

private:
}};

struct {interface.prototype_class} {{
public:
    static void initialize(JS::Realm&, JS::Object&);
    static void define_unforgeable_attributes(JS::Realm&, JS::Object&);

private:
"""
    )
    for attribute in interface.attributes:
        out.write(f"    JS_DECLARE_NATIVE_FUNCTION({attribute_getter_callback_name(attribute)});\n")
        if not attribute.readonly:
            out.write(f"    JS_DECLARE_NATIVE_FUNCTION({attribute_setter_callback_name(attribute)});\n")
    out.write(
        """};

"""
    )


def write_implementation(out: TextIO, includes: GeneratedIncludes, context: GenerationContext) -> None:
    interface = context.module.interface
    if interface is None:
        return

    ensure_interface_is_supported(interface)

    includes.add("LibJS/Runtime/ValueInlines.h")
    includes.add("LibWeb/Bindings/Intrinsics.h")
    includes.add("LibWeb/WebIDL/Tracing.h")
    includes.add_binding(interface.name)
    if interface.parent_name:
        includes.add_binding(interface.parent_name)
    includes.add(implementation_header_for_interface(interface))
    if interface.attributes:
        includes.add("LibJS/Runtime/Error.h")
        includes.add("LibWeb/Bindings/ExceptionOrUtils.h")

    parent_constructor_setup = ""
    if interface.parent_name:
        parent_constructor_setup = f"""    object.set_prototype(&ensure_web_constructor<{interface.parent_name}Prototype>(realm, "{interface.parent_name}"_fly_string));

"""

    parent_prototype = "realm.intrinsics().object_prototype()"
    if interface.parent_name:
        parent_prototype = f'&ensure_web_prototype<{interface.parent_name}Prototype>(realm, "{interface.parent_name}"_fly_string)'

    out.write(
        f"""void {interface.constructor_class}::initialize(JS::Realm& realm, JS::NativeFunction& object)
{{
    auto& vm = realm.vm();
    [[maybe_unused]] u8 default_attributes = JS::Attribute::Enumerable;

{parent_constructor_setup}\
    object.define_direct_property(vm.names.length, JS::Value(0), JS::Attribute::Configurable);
    object.define_direct_property(vm.names.name, JS::PrimitiveString::create(vm, "{interface.name}"_string), JS::Attribute::Configurable);
    object.define_direct_property(vm.names.prototype, &ensure_web_prototype<{interface.prototype_class}>(realm, "{interface.name}"_fly_string), 0);
"""
    )
    define_the_constants(out, context, includes)
    out.write(
        f"""}}

JS::ThrowCompletionOr<GC::Ref<JS::Object>> {interface.constructor_class}::construct([[maybe_unused]] InterfaceConstructor& constructor, [[maybe_unused]] JS::FunctionObject& new_target)
{{
    WebIDL::log_trace(constructor.vm(), "{interface.constructor_class}::construct");
    return constructor.vm().throw_completion<JS::TypeError>(JS::ErrorType::NotAConstructor, "{interface.name}");
}}

void {interface.prototype_class}::initialize(JS::Realm& realm, JS::Object& object)
{{
    [[maybe_unused]] auto& vm = realm.vm();
    [[maybe_unused]] u8 default_attributes = JS::Attribute::Enumerable | JS::Attribute::Configurable | JS::Attribute::Writable;

    object.set_prototype({parent_prototype});
"""
    )
    define_the_regular_attributes(out, context, includes)

    define_the_constants(out, context, includes)
    out.write(
        f"""    object.define_direct_property(vm.well_known_symbol_to_string_tag(), JS::PrimitiveString::create(vm, "{interface.name}"_string), JS::Attribute::Configurable);
}}

void {interface.prototype_class}::define_unforgeable_attributes(JS::Realm& realm, [[maybe_unused]] JS::Object& object)
{{
    [[maybe_unused]] auto& vm = realm.vm();
    [[maybe_unused]] u8 default_attributes = JS::Attribute::Enumerable;
}}

"""
    )
    write_attribute_getters(out, context, includes)
    write_attribute_setters(out, context, includes)


def ensure_interface_is_supported(interface: Interface) -> None:
    if interface.is_namespace:
        raise RuntimeError(f"Unsupported namespace interface '{interface.name}'")
    if interface.is_callback_interface:
        raise RuntimeError(f"Unsupported callback interface '{interface.name}'")

    supported_declarations = {constant.declaration for constant in interface.constants}
    supported_declarations.update(attribute.declaration for attribute in interface.attributes)
    unsupported_declarations = [
        declaration for declaration in interface.member_declarations if declaration not in supported_declarations
    ]
    if unsupported_declarations:
        raise RuntimeError(f"Unsupported non-empty interface '{interface.name}'")


def implementation_header_for_interface(interface: Interface) -> str:
    path = interface.path.with_suffix(".h")
    parts = path.parts
    return str(Path("LibWeb").joinpath(*parts[parts.index("LibWeb") + 1 :]))


# https://webidl.spec.whatwg.org/#define-the-regular-attributes
# To define the regular attributes of interface or namespace definition on target, given realm realm, run the following steps:
def define_the_regular_attributes(out: TextIO, context: GenerationContext, includes: GeneratedIncludes) -> None:
    interface = context.module.interface
    if interface is None:
        return

    # 1. Let attributes be the list of regular attributes that are members of definition.
    attributes = interface.attributes
    if not attributes:
        return
    out.write(
        """    // 1. Let attributes be the list of regular attributes that are members of definition.

"""
    )

    # 2. Remove from attributes all the attributes that are unforgeable.
    attributes = [
        attribute for attribute in attributes if "LegacyUnforgeable" not in attribute.extended_attributes
    ]
    out.write(
        """    // 2. Remove from attributes all the attributes that are unforgeable.

"""
    )

    # 3. Define the attributes attributes of definition on target given realm.
    out.write(
        """    // 3. Define the attributes attributes of definition on target given realm.

"""
    )
    define_the_attributes(out, context, includes, attributes)


# https://webidl.spec.whatwg.org/#define-the-attributes
# To define the attributes attributes of interface or namespace definition on target given realm realm, run the following steps:
def define_the_attributes(
    out: TextIO,
    context: GenerationContext,
    includes: GeneratedIncludes,
    attributes: list[Attribute],
) -> None:
    if not attributes:
        return

    includes.add("LibJS/Runtime/PropertyDescriptor.h")
    out.write("\n")

    # 1. For each attribute attr of attributes:
    for attribute in attributes:
        getter_name = attribute_getter_callback_name(attribute)
        setter_name = attribute_setter_callback_name(attribute)
        cpp_name = attribute_cpp_name(attribute)
        native_getter_name = f"native_{getter_name}"
        native_setter_name = f"native_{setter_name}"
        out.write(
            f"""    // 1. FIXME: If attr is not exposed in realm, then continue.

    // 2. Let getter be the result of creating an attribute getter given attr, definition, and realm.
    auto {native_getter_name} = JS::NativeFunction::create(realm, {getter_name}, 0, "{attribute.name}"_utf16_fly_string, &realm, "get"sv);

    // 3. Let setter be the result of creating an attribute setter given attr, definition, and realm.
"""
        )
        if attribute.readonly:
            out.write(
                f"""    // Note: the algorithm to create an attribute setter returns undefined if attr is read only.
    GC::Ptr<JS::NativeFunction> {native_setter_name};
"""
            )
        else:
            out.write(
                f"""    auto {native_setter_name} = JS::NativeFunction::create(realm, {setter_name}, 1, "{attribute.name}"_utf16_fly_string, &realm, "set"sv);
"""
            )
        configurable = "false" if "LegacyUnforgeable" in attribute.extended_attributes else "true"
        out.write(
            f"""
    // 4. Let configurable be false if attr is unforgeable and true otherwise.
    auto {cpp_name}_configurable = {configurable};

    // 5. Let desc be the PropertyDescriptor{{[[Get]]: getter, [[Set]]: setter, [[Enumerable]]: true, [[Configurable]]: configurable}}.
    JS::PropertyDescriptor {cpp_name}_desc {{
        .get = {native_getter_name},
        .set = {native_setter_name},
        .enumerable = true,
        .configurable = {cpp_name}_configurable,
    }};

    // 6. Let id be attr’s identifier.
    auto {cpp_name}_id = "{attribute.name}"_utf16_fly_string;

    // 7. Perform ! DefinePropertyOrThrow(target, id, desc).
    MUST(object.define_property_or_throw({cpp_name}_id, {cpp_name}_desc));

    // 8. FIXME: If attr’s type is an observable array type with type argument T, then set target’s backing observable array exotic object for attr to the result of creating an observable array exotic object in realm, given T, attr’s set an indexed value algorithm, and attr’s delete an indexed value algorithm.

"""
        )


# https://webidl.spec.whatwg.org/#define-the-constants
# To define the constants of interface, callback interface, or namespace definition on target, given realm realm, run the following steps:
def define_the_constants(out: TextIO, context: GenerationContext, includes: GeneratedIncludes) -> None:
    interface = context.module.interface
    if interface is None:
        return
    if not interface.constants:
        return
    includes.add("LibJS/Runtime/PropertyDescriptor.h")
    out.write("\n")

    # 1. For each constant const that is a member of definition:
    for constant in interface.constants:
        value = type_conversion.idl_value_to_javascript_value(constant.type, constant.value, includes, context)
        out.write(
            f"""    // 1. FIXME: If const is not exposed in realm, then continue.

    // 2. Let value be the result of converting const’s IDL value to a JavaScript value.
    auto constant_{constant.name}_value = {value};

    // 3. Let desc be the PropertyDescriptor{{[[Writable]]: false, [[Enumerable]]: true, [[Configurable]]: false, [[Value]]: value}}.
    JS::PropertyDescriptor constant_{constant.name}_desc {{
        .value = constant_{constant.name}_value,
        .writable = false,
        .enumerable = true,
        .configurable = false,
    }};

    // 4. Let id be const’s identifier.
    auto constant_{constant.name}_id = "{constant.name}"_utf16_fly_string;

    // 5. Perform ! DefinePropertyOrThrow(target, id, desc).
    MUST(object.define_property_or_throw(constant_{constant.name}_id, constant_{constant.name}_desc));

"""
        )


# https://webidl.spec.whatwg.org/#dfn-attribute-getter
# The attribute getter is created as follows, given an attribute attribute, a namespace or interface target, and a realm realm:
def write_attribute_getters(out: TextIO, context: GenerationContext, includes: GeneratedIncludes) -> None:
    interface = context.module.interface
    if interface is None:
        return

    for attribute in interface.attributes:
        write_attribute_getter(out, context, includes, attribute)


def write_attribute_getter(
    out: TextIO,
    context: GenerationContext,
    includes: GeneratedIncludes,
    attribute: Attribute,
) -> None:
    interface = context.module.interface
    if interface is None:
        return

    return_value = type_conversion.idl_value_to_javascript_value(attribute.type, "R", includes, context)
    out.write(
        f"""JS_DEFINE_NATIVE_FUNCTION({interface.prototype_class}::{attribute_getter_callback_name(attribute)})
{{
    WebIDL::log_trace(vm, "{interface.prototype_class}::{attribute_getter_callback_name(attribute)}");
    auto& realm = *vm.current_realm();

    // 1. Let steps be the following series of steps:

    // 1. Try running the following steps:

    // 1. Let idlObject be null.
    [[maybe_unused]] {fully_qualified_name(interface)}* idl_object = nullptr;

    // 2. If target is an interface, and attribute is a regular attribute:

    // 1. Let jsValue be the this value, if it is not null or undefined, or realm’s global object otherwise. (This will subsequently cause a TypeError in a few steps, if the global object does not implement target and [LegacyLenientThis] is not specified.)
    auto js_value = vm.this_value();
    if (js_value.is_nullish())
        js_value = &realm.global_object();

    // 2. FIXME: If jsValue is a platform object, then perform a security check, passing jsValue, attribute’s identifier, and "getter".

    // 3. If jsValue does not implement target, then:
    if (auto impl = js_value.as_if<{fully_qualified_name(interface)}>()) {{
        // 5. Set idlObject to the IDL interface type value that represents a reference to jsValue.
        idl_object = impl.ptr();
    }} else {{
        // 1. FIXME: If attribute was specified with the [LegacyLenientThis] extended attribute, then return undefined.

        // 2. Otherwise, throw a TypeError.
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "{interface.namespaced_name}");
    }}

    // 4. FIXME: If attribute’s type is an observable array type, then return jsValue’s backing observable array exotic object for attribute.

    // 3. Let R be the result of running the getter steps of attribute with idlObject as this.
    auto R = TRY(throw_dom_exception_if_needed(vm, [&] {{ return idl_object->{attribute_cpp_name(attribute)}(); }}));

    // 4. Return the result of converting R to a JavaScript value of the type attribute is declared as.
    return {return_value};

    // 2. And then, if an exception E was thrown:

    // 1. FIXME: If attribute’s type is a promise type, then return ! Call(%Promise.reject%, %Promise%, «E»).

    // 2. Otherwise, end these steps and allow the exception to propagate.

    // 2. Let name be the string "get " prepended to attribute’s identifier.

    // 3. Let F be CreateBuiltinFunction(steps, 0, name, « », realm).

    // 4. Return F.
}}

"""
    )


def write_attribute_setters(out: TextIO, context: GenerationContext, includes: GeneratedIncludes) -> None:
    interface = context.module.interface
    if interface is None:
        return

    for attribute in interface.attributes:
        if attribute.readonly:
            continue
        write_attribute_setter(out, context, includes, attribute)


# https://webidl.spec.whatwg.org/#dfn-attribute-setter
def write_attribute_setter(
    out: TextIO,
    context: GenerationContext,
    includes: GeneratedIncludes,
    attribute: Attribute,
) -> None:
    interface = context.module.interface
    if interface is None:
        return

    includes.add("LibJS/Runtime/Value.h")
    includes.add("LibWeb/WebIDL/Tracing.h")
    conversion = type_conversion.to_idl_value(attribute, "V", includes, context)
    out.write(
        f"""JS_DEFINE_NATIVE_FUNCTION({interface.prototype_class}::{attribute_setter_callback_name(attribute)})
{{
    WebIDL::log_trace(vm, "{interface.prototype_class}::{attribute_setter_callback_name(attribute)}");
    [[maybe_unused]] auto& realm = *vm.current_realm();

    // 1. Let V be undefined.
    auto V = JS::js_undefined();

    // 2. If any arguments were passed, then set V to the value of the first argument passed.
    if (vm.argument_count() > 0)
        V = vm.argument(0);

    // 3. Let id be attribute’s identifier.

    // 4. Let idlObject be null.
    [[maybe_unused]] {fully_qualified_name(interface)}* idl_object = nullptr;

    // 5. If attribute is a regular attribute:

    // 1. Let jsValue be the this value, if it is not null or undefined, or realm’s global object otherwise. (This will subsequently cause a TypeError in a few steps, if the global object does not implement target and [LegacyLenientThis] is not specified.)
    auto js_value = vm.this_value();
    if (js_value.is_nullish())
        js_value = &realm.global_object();

    // 2. FIXME: If jsValue is a platform object, then perform a security check, passing jsValue, attribute’s identifier, and "setter".

    // 3. Let validThis be true if jsValue implements target, or false otherwise.
    if (auto impl = js_value.as_if<{fully_qualified_name(interface)}>()) {{
        idl_object = impl.ptr();
    }} else {{
        // 4. If validThis is false and attribute was not specified with the [LegacyLenientThis] extended attribute, then throw a TypeError.
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "{interface.namespaced_name}");
    }}

"""
    )
    out.write(
        f"""    // 6. Let idlValue be determined as follows:
    // -> Otherwise, idlValue is the result of converting V to an IDL value of attribute’s type.
    auto idl_value = TRY(throw_dom_exception_if_needed(vm, [&] {{ return {conversion}; }}));

    // 7. Run the setter steps of attribute with idlObject as this and idlValue as the value.
    TRY(throw_dom_exception_if_needed(vm, [&] {{ return idl_object->set_{attribute_cpp_name(attribute)}(idl_value); }}));

    // 8. Return undefined.
    return JS::js_undefined();
}}

"""
    )


def attribute_cpp_name(attribute: Attribute) -> str:
    return make_name_acceptable_cpp(title_case_to_snake_case(attribute.name))


def attribute_getter_callback_name(attribute: Attribute) -> str:
    return f"{attribute_cpp_name(attribute)}_getter"


def attribute_setter_callback_name(attribute: Attribute) -> str:
    return f"{attribute_cpp_name(attribute)}_setter"


def fully_qualified_name(interface: Interface) -> str:
    parts = interface.path.parts
    namespace_name = parts[parts.index("LibWeb") + 1]
    return f"{namespace_name}::{interface.implemented_name}"
