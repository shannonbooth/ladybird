# Copyright (c) 2026-present, the Ladybird developers.
#
# SPDX-License-Identifier: BSD-2-Clause

from dataclasses import dataclass
from dataclasses import field
from typing import Optional

from Utils.webidl_parser import CallbackFunction
from Utils.webidl_parser import IDLType
from Utils.webidl_parser import Interface
from Utils.webidl_parser import Module
from Utils.webidl_parser import Typedef


@dataclass
class GenerationContext:
    module: Module
    modules: list[Module] = field(default_factory=list)
    local_types: set[str] = field(init=False)
    callback_functions: dict[str, CallbackFunction] = field(init=False)
    interfaces: dict[str, Interface] = field(init=False)
    typedefs: dict[str, Typedef] = field(init=False)

    def __post_init__(self) -> None:
        if not self.modules:
            self.modules = [self.module]

        self.local_types = {enumeration.name for enumeration in self.module.enumerations}
        self.callback_functions = {
            callback.name: callback
            for module in self.modules
            for callback in module.callback_functions
        }
        self.interfaces = {
            module.interface.name: module.interface
            for module in self.modules
            if module.interface is not None
        }
        self.typedefs = {typedef.name: typedef for module in self.modules for typedef in module.typedefs}

    def is_local_type(self, name: IDLType | str) -> bool:
        return type_name(name) in self.local_types

    def callback_function(self, name: IDLType | str) -> Optional[CallbackFunction]:
        return self.callback_functions.get(type_name(name))

    def interface(self, name: IDLType | str) -> Optional[Interface]:
        return self.interfaces.get(type_name(name))

    def resolve_typedef(self, type_: IDLType | str) -> IDLType:
        resolved_type = type_ if isinstance(type_, IDLType) else IDLType(type_)
        seen_types: set[str] = set()
        while resolved_type.name in self.typedefs:
            if resolved_type.name in seen_types:
                raise RuntimeError(f"Typedef '{resolved_type.name}' resolves recursively")
            seen_types.add(resolved_type.name)

            typedef_type = self.typedefs[resolved_type.name].type
            resolved_type = typedef_type.clone_with_nullable(resolved_type.nullable or typedef_type.nullable)
        return resolved_type


def type_name(type_: IDLType | str) -> str:
    return type_.name if isinstance(type_, IDLType) else type_
