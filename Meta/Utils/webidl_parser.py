# Copyright (c) 2026-present, the Ladybird developers.
#
# SPDX-License-Identifier: BSD-2-Clause

from dataclasses import dataclass
from dataclasses import field
from pathlib import Path
from typing import Dict
from typing import List
from typing import NoReturn
from typing import Optional
from typing import Set

from Utils.lexer import Lexer


@dataclass(eq=False)
class IDLType:
    name: str
    nullable: bool = False

    def __str__(self) -> str:
        nullable_suffix = "?" if self.nullable else ""
        return f"{self.name}{nullable_suffix}"

    def __eq__(self, other: object) -> bool:
        if isinstance(other, IDLType):
            return self.name == other.name and self.nullable == other.nullable
        if isinstance(other, str):
            return str(self) == other
        return False

    def __hash__(self) -> int:
        return hash(str(self))

    def clone_with_nullable(self, nullable: bool) -> "IDLType":
        return IDLType(self.name, nullable)


@dataclass
class Constant:
    declaration: str
    type: IDLType
    name: str
    value: str


@dataclass
class OperationParameter:
    name: str
    type: IDLType
    optional: bool = False
    default_value: Optional[str] = None


@dataclass
class SpecialOperation:
    identifier_type: str
    declaration: str
    return_type: IDLType
    name: str
    parameters: List[OperationParameter]


@dataclass
class Operation:
    declaration: str
    name: str
    return_type: IDLType
    parameters: List[OperationParameter]
    extended_attributes: Dict[str, str] = field(default_factory=dict)
    is_static: bool = False


@dataclass
class Attribute:
    declaration: str
    name: str
    type: IDLType
    readonly: bool = False
    extended_attributes: Dict[str, str] = field(default_factory=dict)


@dataclass
class Interface:
    name: str
    path: Path
    extended_attributes: Dict[str, str] = field(default_factory=dict)
    is_namespace: bool = False
    is_callback_interface: bool = False
    parent_name: str = ""
    member_declarations: List[str] = field(default_factory=list)
    constants: List[Constant] = field(default_factory=list)
    attributes: List[Attribute] = field(default_factory=list)
    operations: List[Operation] = field(default_factory=list)
    named_property_getter: Optional[SpecialOperation] = None
    indexed_property_getter: Optional[SpecialOperation] = None
    has_special_member: bool = False
    implemented_name: str = ""
    namespaced_name: str = ""
    constructor_class: str = ""
    prototype_class: str = ""
    namespace_class: str = ""

    def supports_named_properties(self) -> bool:
        return self.named_property_getter is not None

    def finalize(self) -> None:
        legacy_namespace = self.extended_attributes.get("LegacyNamespace")
        if legacy_namespace:
            self.namespaced_name = f"{legacy_namespace}.{self.name}"
        else:
            self.namespaced_name = self.name

        self.implemented_name = self.extended_attributes.get("ImplementedAs", self.name)
        self.constructor_class = f"{self.implemented_name}Constructor"
        self.prototype_class = f"{self.implemented_name}Prototype"
        self.namespace_class = f"{self.name}Namespace"


@dataclass
class Dictionary:
    name: str
    path: Path
    members: List["DictionaryMember"] = field(default_factory=list)


@dataclass
class DictionaryMember:
    name: str
    type: IDLType
    required: bool = False
    default_value: Optional[str] = None


@dataclass
class CallbackFunction:
    name: str
    path: Path
    return_type: IDLType
    extended_attributes: Dict[str, str] = field(default_factory=dict)


@dataclass
class Enumeration:
    name: str
    path: Path
    values: List[str]
    extended_attributes: Dict[str, str] = field(default_factory=dict)


@dataclass
class Typedef:
    name: str
    path: Path
    type: IDLType


@dataclass
class Module:
    path: Path
    interface: Optional[Interface] = None
    partial_interfaces: List[Interface] = field(default_factory=list)
    callback_functions: List[CallbackFunction] = field(default_factory=list)
    dictionaries: List[Dictionary] = field(default_factory=list)
    enumerations: List[Enumeration] = field(default_factory=list)
    typedefs: List[Typedef] = field(default_factory=list)


@dataclass
class NestingState:
    parentheses_depth: int = 0
    bracket_depth: int = 0
    brace_depth: int = 0
    angle_depth: int = 0

    def is_at_top_level(self) -> bool:
        return not any((self.parentheses_depth, self.bracket_depth, self.brace_depth, self.angle_depth))

    def update_for_character(self, character: str) -> None:
        if character == "(":
            self.parentheses_depth += 1
        elif character == ")":
            self.parentheses_depth = max(self.parentheses_depth - 1, 0)
        elif character == "[":
            self.bracket_depth += 1
        elif character == "]":
            self.bracket_depth = max(self.bracket_depth - 1, 0)
        elif character == "{":
            self.brace_depth += 1
        elif character == "}":
            self.brace_depth = max(self.brace_depth - 1, 0)
        elif character == "<":
            self.angle_depth += 1
        elif character == ">":
            self.angle_depth = max(self.angle_depth - 1, 0)


class ParseError(RuntimeError):
    pass


def parse_module(path: Path, contents: str) -> Module:
    return Parser(path, contents).parse()


class Parser:
    def __init__(self, path: Path, contents: str) -> None:
        self.path = path
        self.contents = contents
        self.lexer = Lexer(contents)

    def parse(self) -> Module:
        module = Module(path=self.path)

        self.consume_whitespace()
        while not self.lexer.is_eof():
            extended_attributes: Dict[str, str] = {}
            if self.lexer.consume_specific("["):
                extended_attributes = self.parse_extended_attributes()

            if self.next_is_keyword("dictionary") or self.next_is_keyword("partial dictionary"):
                dictionary = self.parse_dictionary()
                if dictionary is not None:
                    module.dictionaries.append(dictionary)
            elif self.next_is_keyword("enum"):
                module.enumerations.append(self.parse_enumeration(extended_attributes))
            elif self.next_is_keyword("typedef"):
                module.typedefs.append(self.parse_typedef())
            elif self.next_is_keyword("partial interface mixin"):
                self.skip_braced_declaration()
            elif self.next_is_keyword("partial interface"):
                module.partial_interfaces.append(self.parse_partial_interface(extended_attributes))
            elif self.next_is_keyword("interface mixin"):
                self.skip_braced_declaration()
            elif self.next_is_keyword("partial namespace"):
                self.skip_braced_declaration()
            elif self.next_is_keyword("callback interface"):
                module.interface = self.set_or_check_module_interface(
                    module.interface,
                    self.parse_interface(extended_attributes, is_callback_interface=True),
                )
            elif self.next_is_keyword("callback"):
                module.callback_functions.append(self.parse_callback_function(extended_attributes))
            elif self.next_is_keyword("namespace"):
                module.interface = self.set_or_check_module_interface(
                    module.interface,
                    self.parse_interface(extended_attributes, is_namespace=True),
                )
            elif self.next_is_keyword("interface"):
                module.interface = self.set_or_check_module_interface(
                    module.interface,
                    self.parse_interface(extended_attributes),
                )
            else:
                self.parse_includes_statement()

            self.consume_whitespace()

        return module

    def set_or_check_module_interface(
        self, existing_interface: Optional[Interface], candidate_interface: Interface
    ) -> Interface:
        if existing_interface is None:
            return candidate_interface

        if not existing_interface.is_namespace and existing_interface.name == candidate_interface.name:
            return existing_interface

        self.raise_parse_error(
            "encountered multiple interface, callback interface, or namespace declarations in one file"
        )

    def parse_includes_statement(self) -> None:
        self.parse_identifier_ending_with_space()
        self.consume_whitespace()

        if not self.next_is_keyword("includes"):
            self.raise_parse_error("expected a declaration or an includes statement")

        self.consume_keyword("includes")
        self.consume_whitespace()
        self.parse_identifier_ending_with_space_or(";")
        self.consume_whitespace()
        self.assert_specific(";")

    def parse_partial_interface(self, extended_attributes: Dict[str, str]) -> Interface:
        self.consume_keyword("partial")
        self.consume_whitespace()
        return self.parse_interface(extended_attributes)

    def parse_interface(
        self,
        extended_attributes: Dict[str, str],
        is_namespace: bool = False,
        is_callback_interface: bool = False,
    ) -> Interface:
        if is_callback_interface:
            self.consume_keyword("callback")
            self.consume_whitespace()
            self.consume_keyword("interface")
        elif is_namespace:
            self.consume_keyword("namespace")
        else:
            self.consume_keyword("interface")

        self.consume_whitespace()

        interface = Interface(
            name=self.parse_identifier_ending_with_space_or(":", "{"),
            path=self.path,
            extended_attributes=extended_attributes,
            is_namespace=is_namespace,
            is_callback_interface=is_callback_interface,
        )

        self.consume_whitespace()
        if not is_namespace and self.lexer.consume_specific(":"):
            self.consume_whitespace()
            interface.parent_name = self.parse_identifier_ending_with_space_or("{")
            self.consume_whitespace()

        body_text = self.consume_braced_block()
        self.consume_whitespace()
        self.assert_specific(";")

        if not is_namespace:
            self.parse_interface_body(interface, body_text)

        interface.finalize()
        return interface

    def parse_dictionary(self) -> Optional[Dictionary]:
        is_partial = False
        if self.next_is_keyword("partial"):
            self.consume_keyword("partial")
            self.consume_whitespace()
            is_partial = True

        self.consume_keyword("dictionary")
        self.consume_whitespace()

        dictionary_name = self.parse_identifier_ending_with_space_or(":", "{")
        self.consume_whitespace()

        if self.lexer.consume_specific(":"):
            self.consume_whitespace()
            self.parse_identifier_ending_with_space_or("{")
            self.consume_whitespace()

        self.assert_specific("{")
        members: List[DictionaryMember] = []

        while True:
            self.consume_whitespace()

            if self.lexer.consume_specific("}"):
                self.consume_whitespace()
                self.assert_specific(";")
                break

            if self.lexer.consume_specific("["):
                self.parse_extended_attributes()
                self.consume_whitespace()

            required = False
            if self.next_is_keyword("required"):
                required = True
                self.consume_keyword("required")
                self.consume_whitespace()

            if self.lexer.consume_specific("["):
                self.parse_extended_attributes()
                self.consume_whitespace()

            member_type = self.parse_type()
            self.consume_whitespace()
            member_name = self.parse_identifier_ending_with_space_or("=", ";")
            self.consume_whitespace()

            default_value: Optional[str] = None
            if self.lexer.consume_specific("="):
                if required:
                    self.raise_parse_error("required dictionary member must not have a default value")
                self.consume_whitespace()
                default_value = self.lexer.consume_until(lambda character: character.isspace() or character == ";")
                self.consume_whitespace()

            self.assert_specific(";")

            members.append(
                DictionaryMember(
                    name=member_name,
                    type=member_type,
                    required=required,
                    default_value=default_value,
                )
            )

        members.sort(key=lambda member: member.name)

        if is_partial:
            return None

        return Dictionary(name=dictionary_name, path=self.path, members=members)

    def parse_callback_function(self, extended_attributes: Dict[str, str]) -> CallbackFunction:
        self.consume_keyword("callback")
        self.consume_whitespace()

        name = self.parse_identifier_ending_with_space_or("=")
        self.consume_whitespace()
        self.assert_specific("=")
        self.consume_whitespace()

        return_type = self.parse_type()
        self.consume_whitespace()
        self.consume_statement_text()

        return CallbackFunction(
            name=name,
            path=self.path,
            return_type=return_type,
            extended_attributes=extended_attributes,
        )

    def parse_typedef(self) -> Typedef:
        self.consume_keyword("typedef")
        self.consume_whitespace()

        typedef_type = self.parse_type()
        self.consume_whitespace()

        name = self.parse_identifier_ending_with_space_or(";")
        self.consume_whitespace()
        self.assert_specific(";")

        return Typedef(name=name, path=self.path, type=typedef_type)

    def parse_type(self) -> IDLType:
        if self.lexer.consume_specific("("):
            member_types = [self.parse_type()]
            self.consume_whitespace()
            self.consume_keyword("or")
            self.consume_whitespace()
            member_types.append(self.parse_type())
            self.consume_whitespace()

            while self.lexer.consume_specific("or"):
                self.consume_whitespace()
                member_types.append(self.parse_type())
                self.consume_whitespace()

            self.assert_specific(")")
            return IDLType(f"({' or '.join(str(member_type) for member_type in member_types)})", self.parse_nullable())

        unsigned = self.lexer.consume_specific("unsigned")
        if unsigned:
            self.consume_whitespace()

        unrestricted = self.lexer.consume_specific("unrestricted")
        if unrestricted:
            self.consume_whitespace()

        name = self.lexer.consume_while(lambda character: character.isalnum() or character == "_")
        if not name:
            self.raise_parse_error("type can't be an empty string")

        if name.lower() == "long":
            position_before_whitespace = self.lexer.tell()
            self.consume_whitespace()
            if self.lexer.consume_specific("long"):
                name = "long long"
            else:
                self.lexer.position = position_before_whitespace

        if self.lexer.consume_specific("<"):
            parameters = [self.parse_type()]
            self.consume_whitespace()
            while self.lexer.consume_specific(","):
                self.consume_whitespace()
                parameters.append(self.parse_type())
                self.consume_whitespace()
            self.assert_specific(">")
            name = f"{name}<{', '.join(str(parameter) for parameter in parameters)}>"

        prefixes = []
        if unsigned:
            prefixes.append("unsigned")
        if unrestricted:
            prefixes.append("unrestricted")
        prefixes.append(name)
        return IDLType(" ".join(prefixes), self.parse_nullable())

    def parse_nullable(self) -> bool:
        return self.lexer.consume_specific("?")

    def parse_enumeration(self, extended_attributes: Dict[str, str]) -> Enumeration:
        self.consume_keyword("enum")
        self.consume_whitespace()

        name = self.parse_identifier_ending_with_space_or("{")
        self.consume_whitespace()
        self.assert_specific("{")

        values: List[str] = []
        seen_values: Set[str] = set()

        while not self.lexer.is_eof():
            self.consume_whitespace()
            if self.lexer.consume_specific("}"):
                break

            self.assert_specific('"')
            value = self.lexer.consume_until(lambda character: character == '"')
            self.assert_specific('"')
            self.consume_whitespace()

            if value in seen_values:
                self.raise_parse_error(f"Enumeration {name} contains duplicate member '{value}'")
            seen_values.add(value)
            values.append(value)

            self.consume_whitespace()
            if self.lexer.next_is("}"):
                continue
            self.assert_specific(",")

        self.consume_whitespace()
        self.assert_specific(";")

        return Enumeration(
            name=name,
            path=self.path,
            values=values,
            extended_attributes=extended_attributes,
        )

    def parse_interface_body(self, interface: Interface, body_text: str) -> None:
        for statement in split_top_level_statements(remove_line_comments(body_text)):
            if not statement:
                continue

            parser = Parser(self.path, statement)
            extended_attributes = parser.parse_leading_extended_attributes()
            stripped_statement = parser.remaining_text().strip()
            if not stripped_statement:
                continue
            interface.member_declarations.append(stripped_statement)

            if stripped_statement.startswith("const "):
                interface.constants.append(self.parse_constant(stripped_statement))
                continue

            if stripped_statement.startswith("readonly attribute ") or stripped_statement.startswith("attribute "):
                interface.attributes.append(self.parse_attribute(stripped_statement, extended_attributes))
                continue

            if (
                stripped_statement.startswith("iterable<")
                or stripped_statement.startswith("async iterable<")
                or stripped_statement.startswith("maplike<")
                or stripped_statement.startswith("setlike<")
            ):
                interface.has_special_member = True

            if stripped_statement.startswith("getter "):
                special_operation = self.parse_special_operation(stripped_statement)
                identifier_type = special_operation.identifier_type

                if identifier_type == "DOMString":
                    interface.named_property_getter = special_operation
                elif identifier_type == "unsigned long":
                    interface.indexed_property_getter = special_operation
                else:
                    self.raise_parse_error(
                        f"named/indexed property getter must use DOMString or unsigned long, got '{identifier_type}'"
                    )
                continue

            if stripped_statement.startswith("setter ") or stripped_statement.startswith("deleter "):
                interface.has_special_member = True
                continue

            if "(" in stripped_statement:
                interface.operations.append(self.parse_operation(stripped_statement, extended_attributes))

    def parse_special_operation(self, declaration: str) -> SpecialOperation:
        parser = Parser(self.path, declaration)

        parser.consume_keyword("getter")
        parser.consume_whitespace()

        return_type = parser.parse_type()
        parser.consume_whitespace()

        name = ""
        if not parser.lexer.next_is("("):
            name = parser.parse_identifier_ending_with_space_or("(")
            parser.consume_whitespace()

        parser.assert_specific("(")
        parameters = parser.parse_operation_parameters()
        parser.assert_specific(")")
        parser.consume_whitespace()
        if not parser.lexer.is_eof():
            parser.raise_parse_error("unexpected trailing text after special operation")

        if not parameters:
            parser.raise_parse_error("special operation must have an identifier parameter")

        return SpecialOperation(
            identifier_type=str(parameters[0].type),
            declaration=declaration,
            return_type=return_type,
            name=name,
            parameters=parameters,
        )

    def parse_operation_parameters(self) -> List[OperationParameter]:
        parameters: List[OperationParameter] = []

        self.consume_whitespace()
        while not self.lexer.next_is(")"):
            if self.lexer.consume_specific("["):
                self.parse_extended_attributes()
                self.consume_whitespace()

            optional = False
            if self.next_is_keyword("optional"):
                optional = True
                self.consume_keyword("optional")
                self.consume_whitespace()

            if self.lexer.consume_specific("["):
                self.parse_extended_attributes()
                self.consume_whitespace()

            parameter_type = self.parse_type()
            self.consume_whitespace()

            self.lexer.consume_specific("...")
            self.consume_whitespace()

            parameter_name = self.parse_identifier_ending_with_space_or(",", ")")

            self.consume_whitespace()
            default_value: Optional[str] = None
            if self.lexer.consume_specific("="):
                default_value = self.consume_until_top_level(",", ")").strip()
                self.consume_whitespace()
            parameters.append(
                OperationParameter(
                    name=parameter_name,
                    type=parameter_type,
                    optional=optional,
                    default_value=default_value,
                )
            )
            if not self.lexer.consume_specific(","):
                break
            self.consume_whitespace()

        return parameters

    def parse_operation(self, declaration: str, extended_attributes: Dict[str, str]) -> Operation:
        parser = Parser(self.path, declaration)

        is_static = False
        if parser.next_is_keyword("static"):
            is_static = True
            parser.consume_keyword("static")
            parser.consume_whitespace()

        return_type = parser.parse_type()
        parser.consume_whitespace()
        name = parser.parse_identifier_ending_with_space_or("(")
        parser.consume_whitespace()
        parser.assert_specific("(")
        parameters = parser.parse_operation_parameters()
        parser.assert_specific(")")
        parser.consume_whitespace()
        if not parser.lexer.is_eof():
            parser.raise_parse_error("unexpected trailing text after operation")

        return Operation(
            declaration=declaration,
            name=name,
            return_type=return_type,
            parameters=parameters,
            extended_attributes=extended_attributes,
            is_static=is_static,
        )

    def parse_constant(self, declaration: str) -> Constant:
        parser = Parser(self.path, declaration)

        parser.consume_keyword("const")
        parser.consume_whitespace()

        constant_type = parser.parse_type()
        parser.consume_whitespace()

        name = parser.parse_identifier_ending_with_space_or("=")
        parser.consume_whitespace()
        parser.assert_specific("=")
        parser.consume_whitespace()

        value = parser.lexer.consume_while(lambda character: not character.isspace()).strip()
        parser.consume_whitespace()
        if not parser.lexer.is_eof():
            parser.raise_parse_error("unexpected trailing text after constant value")

        return Constant(
            declaration=declaration,
            type=constant_type,
            name=name,
            value=value,
        )

    def parse_attribute(self, declaration: str, extended_attributes: Dict[str, str]) -> Attribute:
        parser = Parser(self.path, declaration)

        readonly = False
        if parser.next_is_keyword("readonly"):
            readonly = True
            parser.consume_keyword("readonly")
            parser.consume_whitespace()

        parser.consume_keyword("attribute")
        parser.consume_whitespace()

        attribute_type = parser.parse_type()
        parser.consume_whitespace()

        name = parser.parse_identifier_ending_with_space()
        parser.consume_whitespace()
        if not parser.lexer.is_eof():
            parser.raise_parse_error("unexpected trailing text after attribute")

        return Attribute(
            declaration=declaration,
            name=name,
            type=attribute_type,
            readonly=readonly,
            extended_attributes=extended_attributes,
        )

    def parse_extended_attributes(self) -> Dict[str, str]:
        extended_attributes: Dict[str, str] = {}

        while True:
            self.consume_whitespace()
            if self.lexer.consume_specific("]"):
                break

            name = self.parse_identifier_ending_with_space_or("]", "=", ",")
            value = ""
            if self.lexer.consume_specific("="):
                value = self.consume_extended_attribute_value().strip()

            extended_attributes[name] = value
            self.lexer.consume_specific(",")

        self.consume_whitespace()
        return extended_attributes

    def parse_leading_extended_attributes(self) -> Dict[str, str]:
        self.consume_whitespace()
        if self.lexer.consume_specific("["):
            return self.parse_extended_attributes()
        return {}

    def remaining_text(self) -> str:
        return self.contents[self.lexer.tell() :]

    def consume_extended_attribute_value(self) -> str:
        start = self.lexer.tell()
        parentheses_depth = 0

        while not self.lexer.is_eof():
            character = self.lexer.peek()
            if character == "(":
                parentheses_depth += 1
            elif character == ")":
                if parentheses_depth > 0:
                    parentheses_depth -= 1
            elif parentheses_depth == 0 and character in (",", "]"):
                break

            self.lexer.consume()

        return self.contents[start : self.lexer.tell()]

    def skip_braced_declaration(self) -> None:
        while not self.lexer.is_eof() and self.lexer.peek() != "{":
            self.lexer.consume()

        if self.lexer.is_eof():
            self.raise_parse_error("expected '{' while skipping declaration")

        self.consume_braced_block()
        self.consume_whitespace()
        self.assert_specific(";")

    def consume_braced_block(self) -> str:
        self.assert_specific("{")

        start = self.lexer.tell()
        brace_depth = 1
        active_quote = ""

        while not self.lexer.is_eof():
            character = self.lexer.consume()

            if active_quote:
                if character == active_quote:
                    active_quote = ""
                continue

            if character == "/" and self.lexer.peek() == "/":
                self.lexer.ignore_until("\n")
                if self.lexer.peek() == "\n":
                    self.lexer.ignore()
                continue

            if character in ('"', "'"):
                active_quote = character
                continue

            if character == "{":
                brace_depth += 1
            elif character == "}":
                brace_depth -= 1
                if brace_depth == 0:
                    end = self.lexer.tell() - 1
                    return self.contents[start:end]

        self.raise_parse_error("unterminated declaration body")

    def consume_statement_text(self) -> str:
        start = self.lexer.tell()
        nesting_state = NestingState()
        active_quote = ""

        while not self.lexer.is_eof():
            character = self.lexer.consume()

            if active_quote:
                if character == active_quote:
                    active_quote = ""
                continue

            if character == "/" and self.lexer.peek() == "/":
                self.lexer.ignore_until("\n")
                if self.lexer.peek() == "\n":
                    self.lexer.ignore()
                continue

            if character in ('"', "'"):
                active_quote = character
                continue

            if character == ";" and nesting_state.is_at_top_level():
                end = self.lexer.tell() - 1
                return self.contents[start:end]
            nesting_state.update_for_character(character)

        self.raise_parse_error("unterminated statement")

    def consume_until_top_level(self, *terminators: str) -> str:
        start = self.lexer.tell()
        nesting_state = NestingState()
        active_quote = ""

        while not self.lexer.is_eof():
            character = self.lexer.peek()

            if active_quote:
                self.lexer.consume()
                if character == active_quote:
                    active_quote = ""
                continue

            if character in ('"', "'"):
                active_quote = character
                self.lexer.consume()
                continue

            if character in terminators and nesting_state.is_at_top_level():
                return self.contents[start : self.lexer.tell()]

            nesting_state.update_for_character(self.lexer.consume())

        return self.contents[start : self.lexer.tell()]

    def parse_identifier_ending_with_space(self) -> str:
        return self.parse_identifier_ending_with_space_or()

    def parse_identifier_ending_with_space_or(self, *terminators: str) -> str:
        identifier = self.lexer.consume_until(lambda character: character.isspace() or character in terminators)
        return identifier.lstrip("_")

    def consume_whitespace(self) -> None:
        consumed = True
        while consumed:
            consumed = False

            whitespace = self.lexer.consume_while(str.isspace)
            if whitespace:
                consumed = True

            if self.lexer.consume_specific("//"):
                self.lexer.ignore_until("\n")
                if self.lexer.peek() == "\n":
                    self.lexer.ignore()
                consumed = True

    def next_is_keyword(self, keyword: str) -> bool:
        if not self.lexer.next_is(keyword):
            return False

        end = self.lexer.tell() + len(keyword)
        if end >= len(self.contents):
            return True

        trailing_character = self.contents[end]
        return trailing_character.isspace() or trailing_character in "{([;:"

    def consume_keyword(self, keyword: str) -> None:
        if not self.next_is_keyword(keyword):
            self.raise_parse_error(f"expected '{keyword}'")
        self.lexer.ignore(len(keyword))

    def assert_specific(self, expected_character: str) -> None:
        if not self.lexer.consume_specific(expected_character):
            self.raise_parse_error(f"expected '{expected_character}'")

    def raise_parse_error(self, message: str) -> NoReturn:
        line_number = 1
        column_number = 1

        for index, character in enumerate(self.contents):
            if index == self.lexer.tell():
                break
            if character == "\n":
                line_number += 1
                column_number = 1
            else:
                column_number += 1

        raise ParseError(f"{self.path}:{line_number}:{column_number}: error: {message}")


def remove_line_comments(text: str) -> str:
    lines = []
    for line in text.splitlines():
        comment_index = line.find("//")
        if comment_index != -1:
            line = line[:comment_index]
        lines.append(line)
    return "\n".join(lines)


def split_top_level_statements(text: str) -> List[str]:
    statements: List[str] = []
    start = 0
    nesting_state = NestingState()
    active_quote = ""

    for index, character in enumerate(text):
        if active_quote:
            if character == active_quote:
                active_quote = ""
            continue

        if character in ('"', "'"):
            active_quote = character
            continue

        if character == ";" and nesting_state.is_at_top_level():
            statements.append(text[start:index].strip())
            start = index + 1
            continue

        nesting_state.update_for_character(character)

    trailing_text = text[start:].strip()
    if trailing_text:
        statements.append(trailing_text)

    return statements
