# Copyright (c) 2026-present, the Ladybird developers.
#
# SPDX-License-Identifier: BSD-2-Clause

from pathlib import Path
from typing import TextIO

from Generators.libweb_bindings import type_conversion
from Generators.libweb_bindings.context import GenerationContext
from Generators.libweb_bindings.includes import GeneratedIncludes
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
}};

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
    define_the_constants(out, interface, includes)
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
    define_the_constants(out, interface, includes)
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


def ensure_interface_is_supported(interface: Interface) -> None:
    if interface.is_namespace:
        raise RuntimeError(f"Unsupported namespace interface '{interface.name}'")
    if interface.is_callback_interface:
        raise RuntimeError(f"Unsupported callback interface '{interface.name}'")

    supported_declarations = {constant.declaration for constant in interface.constants}
    unsupported_declarations = [
        declaration for declaration in interface.member_declarations if declaration not in supported_declarations
    ]
    if unsupported_declarations:
        raise RuntimeError(f"Unsupported non-empty interface '{interface.name}'")


def implementation_header_for_interface(interface: Interface) -> str:
    path = interface.path.with_suffix(".h")
    parts = path.parts
    return str(Path("LibWeb").joinpath(*parts[parts.index("LibWeb") + 1 :]))


# https://webidl.spec.whatwg.org/#define-the-constants
# To define the constants of interface, callback interface, or namespace definition on target, given realm realm, run the following steps:
def define_the_constants(out: TextIO, interface: Interface, includes: GeneratedIncludes) -> None:
    if not interface.constants:
        return
    includes.add("LibJS/Runtime/PropertyDescriptor.h")
    out.write("\n")

    # 1. For each constant const that is a member of definition:
    for constant in interface.constants:
        value = type_conversion.idl_value_to_javascript_value(constant.type, constant.value, includes)
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
