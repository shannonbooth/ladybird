# Copyright (c) 2026-present, the Ladybird developers.
#
# SPDX-License-Identifier: BSD-2-Clause

from dataclasses import dataclass
from dataclasses import field
from dataclasses import replace
from typing import Optional
from typing import Union

from Utils.webidl_parser import CallbackFunction
from Utils.webidl_parser import Dictionary
from Utils.webidl_parser import Enumeration
from Utils.webidl_parser import IDLParameterizedType
from Utils.webidl_parser import IDLType
from Utils.webidl_parser import IDLUnionType
from Utils.webidl_parser import IncludedMixin
from Utils.webidl_parser import Interface
from Utils.webidl_parser import Module
from Utils.webidl_parser import Typedef


@dataclass
class GenerationContext:
    module: Module
    modules: list[Module] = field(default_factory=list)
    local_types: set[str] = field(init=False)
    callback_functions: dict[str, CallbackFunction] = field(init=False)
    dictionaries: dict[str, Dictionary] = field(init=False)
    enumerations: dict[str, Enumeration] = field(init=False)
    interfaces: dict[str, Interface] = field(init=False)
    typedefs: dict[str, Typedef] = field(init=False)
    partial_interfaces: dict[str, list[Interface]] = field(init=False)
    mixins: dict[str, Interface] = field(init=False)
    partial_mixins: dict[str, list[Interface]] = field(init=False)
    included_mixins: dict[str, list[IncludedMixin]] = field(init=False)

    def __post_init__(self) -> None:
        if not self.modules:
            self.modules = [self.module]

        self.local_types = {enumeration.name for enumeration in self.module.enumerations}
        self.local_types.update(dictionary.name for dictionary in self.module.dictionaries)
        self.callback_functions = {
            callback.name: callback for module in self.modules for callback in module.callback_functions
        }
        self.dictionaries = {
            dictionary.name: dictionary for module in self.modules for dictionary in module.dictionaries
        }
        self.enumerations = {
            enumeration.name: enumeration for module in self.modules for enumeration in module.enumerations
        }
        self.interfaces = {
            module.interface.name: module.interface for module in self.modules if module.interface is not None
        }
        self.partial_interfaces = {}
        for module in self.modules:
            for partial_interface in module.partial_interfaces:
                self.partial_interfaces.setdefault(partial_interface.name, []).append(partial_interface)
        self.mixins = {mixin.name: mixin for module in self.modules for mixin in module.mixins}
        self.partial_mixins = {}
        for module in self.modules:
            for partial_mixin in module.partial_mixins:
                self.partial_mixins.setdefault(partial_mixin.name, []).append(partial_mixin)
        self.included_mixins = {}
        for module in self.modules:
            for included_mixin in module.included_mixins:
                self.included_mixins.setdefault(included_mixin.interface_name, []).append(included_mixin)
        self.typedefs = {typedef.name: typedef for module in self.modules for typedef in module.typedefs}

    def is_local_type(self, name: Union[IDLType, str]) -> bool:
        return type_name(name) in self.local_types

    def callback_function(self, name: Union[IDLType, str]) -> Optional[CallbackFunction]:
        return self.callback_functions.get(type_name(name))

    def dictionary(self, name: Union[IDLType, str]) -> Optional[Dictionary]:
        return self.dictionaries.get(type_name(name))

    def enumeration(self, name: Union[IDLType, str]) -> Optional[Enumeration]:
        return self.enumerations.get(type_name(name))

    def interface(self, name: Union[IDLType, str]) -> Optional[Interface]:
        return self.interfaces.get(type_name(name))

    def interface_for_generation(self) -> Optional[Interface]:
        interface = self.module.interface
        if interface is None:
            return None

        partial_interfaces = self.partial_interfaces.get(interface.name, [])
        included_mixins = self.included_mixins.get(interface.name, [])
        if not partial_interfaces and not included_mixins:
            return interface

        merged_interface = replace(
            interface,
            member_declarations=list(interface.member_declarations),
            constants=list(interface.constants),
            attributes=list(interface.attributes),
            operations=list(interface.operations),
            constructors=list(interface.constructors),
        )
        for partial_interface in partial_interfaces:
            merge_interface_members(merged_interface, partial_interface)

        for included_mixin in included_mixins:
            mixin = self.mixin_for_generation(included_mixin.mixin_name)
            if mixin is None:
                raise RuntimeError(f"Included mixin '{included_mixin.mixin_name}' does not exist")
            merge_interface_members(merged_interface, mixin)
        return merged_interface

    def mixin_for_generation(self, name: str) -> Optional[Interface]:
        mixin = self.mixins.get(name)
        if mixin is None:
            return None

        partial_mixins = self.partial_mixins.get(mixin.name, [])
        if not partial_mixins:
            return mixin

        merged_mixin = replace(
            mixin,
            member_declarations=list(mixin.member_declarations),
            constants=list(mixin.constants),
            attributes=list(mixin.attributes),
            operations=list(mixin.operations),
            constructors=list(mixin.constructors),
        )
        for partial_mixin in partial_mixins:
            merge_interface_members(merged_mixin, partial_mixin)
        return merged_mixin

    def resolve_typedef(self, type_: Union[IDLType, str]) -> IDLType:
        resolved_type = type_ if isinstance(type_, IDLType) else IDLType(type_)

        if isinstance(resolved_type, IDLUnionType):
            return IDLUnionType(
                [self.resolve_typedef(member_type) for member_type in resolved_type.member_types],
                resolved_type.nullable,
            )

        if isinstance(resolved_type, IDLParameterizedType):
            return IDLParameterizedType(
                resolved_type.name,
                [self.resolve_typedef(parameter) for parameter in resolved_type.parameters],
                resolved_type.nullable,
            )

        seen_types: set[str] = set()
        while resolved_type.name in self.typedefs:
            if resolved_type.name in seen_types:
                raise RuntimeError(f"Typedef '{resolved_type.name}' resolves recursively")
            seen_types.add(resolved_type.name)

            typedef_type = self.typedefs[resolved_type.name].type
            resolved_type = typedef_type.clone_with_nullable(resolved_type.nullable or typedef_type.nullable)

            if isinstance(resolved_type, IDLUnionType):
                return IDLUnionType(
                    [self.resolve_typedef(member_type) for member_type in resolved_type.member_types],
                    resolved_type.nullable,
                )

            if isinstance(resolved_type, IDLParameterizedType):
                return IDLParameterizedType(
                    resolved_type.name,
                    [self.resolve_typedef(parameter) for parameter in resolved_type.parameters],
                    resolved_type.nullable,
                )

        return resolved_type


def type_name(type_: Union[IDLType, str]) -> str:
    return type_.name if isinstance(type_, IDLType) else type_


def merge_interface_members(target: Interface, source: Interface) -> None:
    target.member_declarations.extend(source.member_declarations)
    target.constants.extend(source.constants)
    target.attributes.extend(source.attributes)
    target.operations.extend(source.operations)
    target.constructors.extend(source.constructors)
    target.has_special_member = target.has_special_member or source.has_special_member
    target.named_property_getter = target.named_property_getter or source.named_property_getter
    target.indexed_property_getter = target.indexed_property_getter or source.indexed_property_getter
    target.named_property_setter = target.named_property_setter or source.named_property_setter
    target.named_property_deleter = target.named_property_deleter or source.named_property_deleter
    target.indexed_property_setter = target.indexed_property_setter or source.indexed_property_setter
    target.iterable = target.iterable or source.iterable
