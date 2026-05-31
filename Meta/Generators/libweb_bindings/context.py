# Copyright (c) 2026-present, the Ladybird developers.
#
# SPDX-License-Identifier: BSD-2-Clause

from dataclasses import dataclass
from dataclasses import field
from typing import Optional

from Utils.webidl_parser import CallbackFunction
from Utils.webidl_parser import Module
from Utils.webidl_parser import Typedef


@dataclass
class GenerationContext:
    module: Module
    modules: list[Module] = field(default_factory=list)
    local_types: set[str] = field(init=False)
    callback_functions: dict[str, CallbackFunction] = field(init=False)
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
        self.typedefs = {typedef.name: typedef for module in self.modules for typedef in module.typedefs}

    def is_local_type(self, name: str) -> bool:
        return name in self.local_types

    def callback_function(self, name: str) -> Optional[CallbackFunction]:
        return self.callback_functions.get(name)

    def resolve_typedef(self, name: str) -> str:
        seen_types: set[str] = set()
        while name in self.typedefs:
            if name in seen_types:
                raise RuntimeError(f"Typedef '{name}' resolves recursively")
            seen_types.add(name)
            name = self.typedefs[name].type
        return name
