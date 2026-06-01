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
        if not partial_interfaces:
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
            merged_interface.member_declarations.extend(partial_interface.member_declarations)
            merged_interface.constants.extend(partial_interface.constants)
            merged_interface.attributes.extend(partial_interface.attributes)
            merged_interface.operations.extend(partial_interface.operations)
            merged_interface.constructors.extend(partial_interface.constructors)
            merged_interface.has_special_member = (
                merged_interface.has_special_member or partial_interface.has_special_member
            )
            merged_interface.named_property_getter = (
                merged_interface.named_property_getter or partial_interface.named_property_getter
            )
            merged_interface.indexed_property_getter = (
                merged_interface.indexed_property_getter or partial_interface.indexed_property_getter
            )
            merged_interface.named_property_setter = (
                merged_interface.named_property_setter or partial_interface.named_property_setter
            )
            merged_interface.named_property_deleter = (
                merged_interface.named_property_deleter or partial_interface.named_property_deleter
            )
            merged_interface.indexed_property_setter = (
                merged_interface.indexed_property_setter or partial_interface.indexed_property_setter
            )
            merged_interface.iterable = merged_interface.iterable or partial_interface.iterable
        return merged_interface

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
