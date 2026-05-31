# Copyright (c) 2026-present, the Ladybird developers.
#
# SPDX-License-Identifier: BSD-2-Clause

from dataclasses import dataclass
from dataclasses import field
from typing import Optional

from Utils.webidl_parser import CallbackFunction
from Utils.webidl_parser import Module


@dataclass
class GenerationContext:
    module: Module
    local_types: set[str] = field(init=False)
    callback_functions: dict[str, CallbackFunction] = field(init=False)

    def __post_init__(self) -> None:
        self.local_types = {enumeration.name for enumeration in self.module.enumerations}
        self.callback_functions = {callback.name: callback for callback in self.module.callback_functions}

    def is_local_type(self, name: str) -> bool:
        return name in self.local_types

    def callback_function(self, name: str) -> Optional[CallbackFunction]:
        return self.callback_functions.get(name)
