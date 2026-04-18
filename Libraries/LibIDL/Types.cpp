/*
 * Copyright (c) 2022, Sam Atkins <atkinssj@serenityos.org>
 * Copyright (c) 2023, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Debug.h>
#include <LibIDL/Types.h>

namespace IDL {

static NonnullRefPtr<Type const> clone_type(Type const& type)
{
    if (is<ParameterizedType>(type)) {
        Vector<NonnullRefPtr<Type const>> parameters;
        for (auto const& parameter : type.as_parameterized().parameters())
            parameters.append(clone_type(*parameter));
        return adopt_ref(*new ParameterizedType(type.name(), type.is_nullable(), move(parameters)));
    }

    if (is<UnionType>(type)) {
        Vector<NonnullRefPtr<Type const>> member_types;
        for (auto const& member_type : type.as_union().member_types())
            member_types.append(clone_type(*member_type));
        return adopt_ref(*new UnionType(type.name(), type.is_nullable(), move(member_types)));
    }

    return adopt_ref(*new Type(type.name(), type.is_nullable()));
}

static void resolve_union_typedefs(Interface& interface, UnionType& union_);

static void resolve_typedef(Interface& interface, NonnullRefPtr<Type const>& type, HashMap<ByteString, ByteString>* extended_attributes = nullptr)
{
    if (is<ParameterizedType>(*type)) {
        auto& parameterized_type = const_cast<Type&>(*type).as_parameterized();
        auto& parameters = static_cast<Vector<NonnullRefPtr<Type const>>&>(parameterized_type.parameters());
        for (auto& parameter : parameters)
            resolve_typedef(interface, parameter);
        return;
    }

    if (is<UnionType>(*type) && type->name().is_empty()) {
        resolve_union_typedefs(interface, const_cast<Type&>(*type).as_union());
        return;
    }

    auto typedef_ = interface.context->get_typedef(type->name());
    if (!typedef_.has_value())
        return;

    bool nullable = type->is_nullable();
    type = clone_type(typedef_->type);
    const_cast<Type&>(*type).set_nullable(nullable);
    if (extended_attributes) {
        for (auto& attribute : typedef_->extended_attributes)
            extended_attributes->set(attribute.key, attribute.value);
    }

    if (is<UnionType>(*type))
        resolve_union_typedefs(interface, const_cast<Type&>(*type).as_union());
}

static void resolve_union_typedefs(Interface& interface, UnionType& union_)
{
    auto& member_types = static_cast<Vector<NonnullRefPtr<Type const>>&>(union_.member_types());
    for (auto& member_type : member_types)
        resolve_typedef(interface, member_type);
}

static void resolve_parameters_typedefs(Interface& interface, Vector<Parameter>& parameters)
{
    for (auto& parameter : parameters)
        resolve_typedef(interface, parameter.type, &parameter.extended_attributes);
}

template<typename FunctionType>
static void resolve_function_typedefs(Interface& interface, FunctionType& function)
{
    resolve_typedef(interface, function.return_type);
    resolve_parameters_typedefs(interface, function.parameters);
}

template<typename FunctionType, typename OverloadSetType>
static void build_function_overload_sets(Vector<FunctionType>& functions, OverloadSetType& overload_sets)
{
    overload_sets.clear();
    for (auto& function : functions) {
        function.overload_index = 0;
        function.is_overloaded = false;
        if (function.extended_attributes.contains("FIXME"))
            continue;
        auto& overload_set = overload_sets.ensure(function.name);
        function.overload_index = overload_set.size();
        overload_set.append(function);
    }
    for (auto& overload_set : overload_sets) {
        if (overload_set.value.size() == 1)
            continue;
        for (auto& overloaded_function : overload_set.value)
            overloaded_function.is_overloaded = true;
    }
}

static void build_constructor_overload_sets(Vector<Constructor>& constructors, OrderedHashMap<ByteString, Vector<Constructor&>>& overload_sets)
{
    overload_sets.clear();
    for (auto& constructor : constructors) {
        constructor.overload_index = 0;
        constructor.is_overloaded = false;
        if (constructor.extended_attributes.contains("FIXME"))
            continue;
        auto& overload_set = overload_sets.ensure(constructor.name);
        constructor.overload_index = overload_set.size();
        overload_set.append(constructor);
    }
    for (auto& overload_set : overload_sets) {
        if (overload_set.value.size() == 1)
            continue;
        for (auto& overloaded_constructor : overload_set.value)
            overloaded_constructor.is_overloaded = true;
    }
}

static void rebuild_overload_sets(Interface& interface)
{
    build_function_overload_sets(interface.functions, interface.overload_sets);
    build_function_overload_sets(interface.static_functions, interface.static_overload_sets);
    build_constructor_overload_sets(interface.constructors, interface.constructor_overload_sets);
}

static bool types_match(Type const& first, Type const& second)
{
    if (first.kind() != second.kind() || first.name() != second.name() || first.is_nullable() != second.is_nullable())
        return false;

    if (is<ParameterizedType>(first)) {
        auto const& first_parameters = first.as_parameterized().parameters();
        auto const& second_parameters = second.as_parameterized().parameters();
        if (first_parameters.size() != second_parameters.size())
            return false;
        for (auto i = 0u; i < first_parameters.size(); ++i) {
            if (!types_match(*first_parameters[i], *second_parameters[i]))
                return false;
        }
    }

    if (is<UnionType>(first)) {
        auto const& first_member_types = first.as_union().member_types();
        auto const& second_member_types = second.as_union().member_types();
        if (first_member_types.size() != second_member_types.size())
            return false;
        for (auto i = 0u; i < first_member_types.size(); ++i) {
            if (!types_match(*first_member_types[i], *second_member_types[i]))
                return false;
        }
    }

    return true;
}

static bool parameters_match(Parameter const& first, Parameter const& second)
{
    return types_match(*first.type, *second.type)
        && first.optional == second.optional
        && first.variadic == second.variadic
        && first.optional_default_value == second.optional_default_value;
}

static bool functions_match(Function const& first, Function const& second)
{
    if (first.name != second.name || !types_match(*first.return_type, *second.return_type) || first.parameters.size() != second.parameters.size())
        return false;

    for (auto i = 0u; i < first.parameters.size(); ++i) {
        if (!parameters_match(first.parameters[i], second.parameters[i]))
            return false;
    }

    return true;
}

static bool attributes_match(Attribute const& first, Attribute const& second)
{
    return first.name == second.name
        && first.inherit == second.inherit
        && first.readonly == second.readonly
        && types_match(*first.type, *second.type);
}

static bool constants_match(Constant const& first, Constant const& second)
{
    return first.name == second.name
        && first.value == second.value
        && types_match(*first.type, *second.type);
}

static bool should_apply_partial(Interface const& interface, Interface const& partial)
{
    auto partial_exposed = partial.extended_attributes.get("Exposed"sv);
    if (!partial_exposed.has_value() || partial_exposed.value() != "Nobody"sv)
        return true;

    auto interface_exposed = interface.extended_attributes.get("Exposed"sv);
    return interface_exposed.has_value() && interface_exposed.value() == "Nobody"sv;
}

ParameterizedType const& Type::as_parameterized() const
{
    return as<ParameterizedType const>(*this);
}

ParameterizedType& Type::as_parameterized()
{
    return as<ParameterizedType>(*this);
}

UnionType const& Type::as_union() const
{
    return as<UnionType const>(*this);
}

UnionType& Type::as_union()
{
    return as<UnionType>(*this);
}

// https://webidl.spec.whatwg.org/#dfn-includes-a-nullable-type
bool Type::includes_nullable_type() const
{
    // A type includes a nullable type if:
    // - the type is a nullable type, or
    if (is_nullable())
        return true;

    // FIXME: - the type is an annotated type and its inner type is a nullable type, or

    // - the type is a union type and its number of nullable member types is 1.
    if (is_union() && as_union().number_of_nullable_member_types() == 1)
        return true;

    return false;
}

// https://webidl.spec.whatwg.org/#dfn-includes-undefined
bool Type::includes_undefined() const
{
    // A type includes undefined if:
    // - the type is undefined, or
    if (is_undefined())
        return true;

    // - the type is a nullable type and its inner type includes undefined, or
    //   NOTE: We don't treat nullable as its own type, so this is handled by the other cases.

    // FIXME: - the type is an annotated type and its inner type includes undefined, or

    // - the type is a union type and one of its member types includes undefined.
    if (is_union())
        return as_union().member_types().contains([](auto& type) { return type->includes_undefined(); });

    return false;
}

// https://webidl.spec.whatwg.org/#dfn-distinguishable
bool Type::is_distinguishable_from(IDL::Interface const& interface, IDL::Type const& other) const
{
    auto has_dictionary = [&interface](ByteString const& name) {
        return interface.context->get_dictionary(name).has_value();
    };

    // 1. If one type includes a nullable type and the other type either includes a nullable type,
    //    is a union type with flattened member types including a dictionary type, or is a dictionary type,
    //    return false.
    if (includes_nullable_type() && (other.includes_nullable_type() || (other.is_union() && any_of(other.as_union().flattened_member_types(), [&has_dictionary](auto const& type) { return has_dictionary(type->name()); })) || has_dictionary(other.name())))
        return false;

    // 2. If both types are either a union type or nullable union type, return true if each member type
    //    of the one is distinguishable with each member type of the other, or false otherwise.
    if (is_union() && other.is_union()) {
        auto const& this_union = as_union();
        auto const& other_union = other.as_union();

        for (auto& this_member_type : this_union.member_types()) {
            for (auto& other_member_type : other_union.member_types()) {
                if (!this_member_type->is_distinguishable_from(interface, other_member_type))
                    return false;
            }
        }
        return true;
    }

    // 3. If one type is a union type or nullable union type, return true if each member type of the union
    //    type is distinguishable with the non-union type, or false otherwise.
    if (is_union() || other.is_union()) {
        auto const& the_union = is_union() ? as_union() : other.as_union();
        auto const& non_union = is_union() ? other : *this;

        for (auto& member_type : the_union.member_types()) {
            if (!non_union.is_distinguishable_from(interface, member_type))
                return false;
        }
        return true;
    }

    // 4. Consider the two "innermost" types derived by taking each type’s inner type if it is an annotated type,
    //    and then taking its inner type inner type if the result is a nullable type. If these two innermost types
    //    appear or are in categories appearing in the following table and there is a “●” mark in the corresponding
    //    entry or there is a letter in the corresponding entry and the designated additional requirement below the
    //    table is satisfied, then return true. Otherwise return false.
    auto const& this_innermost_type = innermost_type();
    auto const& other_innermost_type = other.innermost_type();

    enum class DistinguishabilityCategory {
        Undefined,
        Boolean,
        Numeric,
        BigInt,
        String,
        Object,
        Symbol,
        InterfaceLike,
        CallbackFunction,
        DictionaryLike,
        SequenceLike,
        __Count
    };

    // See https://webidl.spec.whatwg.org/#distinguishable-table
    // clang-format off
    static constexpr bool table[to_underlying(DistinguishabilityCategory::__Count)][to_underlying(DistinguishabilityCategory::__Count)] {
        {false,  true,  true,  true,  true,  true,  true,  true,  true, false,  true},
        { true, false,  true,  true,  true,  true,  true,  true,  true,  true,  true},
        { true,  true, false,  true,  true,  true,  true,  true,  true,  true,  true},
        { true,  true,  true, false,  true,  true,  true,  true,  true,  true,  true},
        { true,  true,  true,  true, false,  true,  true,  true,  true,  true,  true},
        { true,  true,  true,  true,  true, false,  true, false, false, false, false},
        { true,  true,  true,  true,  true,  true, false,  true,  true,  true,  true},
        { true,  true,  true,  true,  true, false,  true, false,  true,  true,  true},
        { true,  true,  true,  true,  true, false,  true,  true, false, false,  true},
        {false,  true,  true,  true,  true, false,  true,  true, false, false,  true},
        { true,  true,  true,  true,  true, false,  true,  true,  true,  true, false},
    };
    // clang-format on

    auto determine_category = [&interface](Type const& type) -> DistinguishabilityCategory {
        if (type.is_undefined())
            return DistinguishabilityCategory::Undefined;
        if (type.is_boolean())
            return DistinguishabilityCategory::Boolean;
        if (type.is_numeric())
            return DistinguishabilityCategory::Numeric;
        if (type.is_bigint())
            return DistinguishabilityCategory::BigInt;
        if (type.is_string())
            return DistinguishabilityCategory::String;
        if (type.is_object())
            return DistinguishabilityCategory::Object;
        if (type.is_symbol())
            return DistinguishabilityCategory::Symbol;
        // FIXME: InterfaceLike - see below
        // FIXME: CallbackFunction
        // DictionaryLike
        // * Dictionary Types
        // * Record Types
        // FIXME: * Callback Interface Types
        if (interface.context->get_dictionary(type.name()).has_value() || (type.is_parameterized() && type.name() == "record"sv))
            return DistinguishabilityCategory::DictionaryLike;
        // FIXME: Frozen array types are included in "sequence-like"
        if (type.is_sequence())
            return DistinguishabilityCategory::SequenceLike;

        // FIXME: For lack of a better way of determining if something is an interface type, this just assumes anything we don't recognise is one.
        dbgln_if(IDL_DEBUG, "Unable to determine category for type named '{}', assuming it's an interface type.", type.name());
        return DistinguishabilityCategory::InterfaceLike;
    };

    auto this_distinguishability = determine_category(this_innermost_type);
    auto other_distinguishability = determine_category(other_innermost_type);

    if (this_distinguishability == DistinguishabilityCategory::InterfaceLike && other_distinguishability == DistinguishabilityCategory::InterfaceLike) {
        // The two identified interface-like types are not the same, and
        // FIXME: no single platform object implements both interface-like types.
        return this_innermost_type.name() != other_innermost_type.name();
    }

    return table[to_underlying(this_distinguishability)][to_underlying(other_distinguishability)];
}

// https://webidl.spec.whatwg.org/#dfn-json-types
bool Type::is_json(Interface const& interface) const
{
    // The JSON types are:
    // - numeric types,
    if (is_numeric())
        return true;

    // - boolean,
    if (is_boolean())
        return true;

    // - string types,
    if (is_string() || interface.context->get_enumeration(m_name).has_value())
        return true;

    // - object,
    if (is_object())
        return true;

    // - nullable types whose inner type is a JSON type,
    // - annotated types whose inner type is a JSON type,
    // NOTE: We don't separate nullable and annotated into separate types.

    // - union types whose member types are JSON types,
    if (is_union()) {
        auto const& union_type = as_union();

        for (auto const& type : union_type.member_types()) {
            if (!type->is_json(interface))
                return false;
        }

        return true;
    }

    // - typedefs whose type being given a new name is a JSON type,
    if (auto typedef_ = interface.context->get_typedef(m_name); typedef_.has_value())
        return typedef_->type->is_json(interface);

    // - sequence types whose parameterized type is a JSON type,
    // - frozen array types whose parameterized type is a JSON type,
    // - records where all of their values are JSON types,
    if (is_parameterized() && m_name.is_one_of("sequence", "FrozenArray", "record")) {
        auto const& parameterized_type = as_parameterized();

        for (auto const& parameter : parameterized_type.parameters()) {
            if (!parameter->is_json(interface))
                return false;
        }

        return true;
    }

    // - dictionary types where the types of all members declared on the dictionary and all its inherited dictionaries are JSON types,
    auto dictionary = interface.context->get_dictionary(m_name);

    if (dictionary.has_value()) {
        for (auto const& member : dictionary->members) {
            if (!member.type->is_json(interface))
                return false;
        }

        return true;
    }

    // - interface types that have a toJSON operation declared on themselves or one of their inherited interfaces.
    Optional<Interface const&> current_interface_for_to_json;
    if (m_name == interface.name) {
        current_interface_for_to_json = interface;
    } else {
        current_interface_for_to_json = interface.context->get_interface(m_name);
    }

    while (current_interface_for_to_json.has_value()) {
        auto to_json_iterator = current_interface_for_to_json->functions.find_if([](IDL::Function const& function) {
            return function.name == "toJSON"sv;
        });

        if (to_json_iterator != current_interface_for_to_json->functions.end())
            return true;

        if (current_interface_for_to_json->parent_name.is_empty())
            break;

        current_interface_for_to_json = current_interface_for_to_json->context->get_interface(current_interface_for_to_json->parent_name);
    }

    return false;
}

Module& Context::create_module(ByteString own_path)
{
    auto module = make<Module>();
    module->context = this;
    module->own_path = move(own_path);
    auto& module_ref = *module;
    owned_modules.append(move(module));
    return module_ref;
}

Interface& Context::create_interface(Module& module)
{
    NonnullRefPtr<Context> self = *this;
    auto interface = make<Interface>(move(self));
    auto& interface_ref = *interface;
    owned_interfaces.append(move(interface));
    interface_modules.set(&interface_ref, &module);
    return interface_ref;
}

Module& Context::module_for(Interface& interface) const
{
    return const_cast<Module&>(module_for(static_cast<Interface const&>(interface)));
}

Module const& Context::module_for(Interface const& interface) const
{
    auto module = interface_modules.get(&interface);
    VERIFY(module.has_value());
    return *module.value();
}

void Context::register_declaration(ByteString const& name, DeclarationKind kind, Module const& owner, bool is_original_definition)
{
    declarations.ensure(name).append({ name, kind, &owner, is_original_definition });
}

void Context::register_interface(Interface const& interface, Module const& module)
{
    interfaces.set(interface.name, &interface);
    register_declaration(interface.name, DeclarationKind::Interface, module);
}

void Context::register_partial_interface(Interface const& interface, Module const& module)
{
    partial_interfaces.ensure(interface.name).append(&interface);
    register_declaration(interface.name, DeclarationKind::PartialInterface, module);
}

void Context::register_partial_namespace(Interface const& interface, Module const& module)
{
    partial_namespaces.ensure(interface.name).append(&interface);
    register_declaration(interface.name, DeclarationKind::PartialNamespace, module);
}

void Context::register_partial_mixin(Interface const& interface, Module const& module)
{
    partial_mixins.ensure(interface.name).append(&interface);
    register_declaration(interface.name, DeclarationKind::PartialMixin, module);
}

void Context::register_dictionary(ByteString const& name, Dictionary dictionary, Module const& module)
{
    auto is_original_definition = dictionary.is_original_definition;
    dictionaries.set(name, move(dictionary));
    register_declaration(name, DeclarationKind::Dictionary, module, is_original_definition);
}

void Context::register_partial_dictionary(ByteString const& name, Dictionary dictionary, Module const& module)
{
    auto is_original_definition = dictionary.is_original_definition;
    partial_dictionaries.ensure(name).append(move(dictionary));
    register_declaration(name, DeclarationKind::PartialDictionary, module, is_original_definition);
}

void Context::register_enumeration(ByteString const& name, Enumeration enumeration, Module const& module)
{
    auto is_original_definition = enumeration.is_original_definition;
    enumerations.set(name, move(enumeration));
    register_declaration(name, DeclarationKind::Enumeration, module, is_original_definition);
}

void Context::register_mixin(ByteString const& name, Interface& mixin, Module const& module)
{
    mixins.set(name, &mixin);
    register_declaration(name, DeclarationKind::Mixin, module);
}

void Context::register_included_mixin(ByteString const& interface_name, ByteString const& mixin_name)
{
    included_mixins.ensure(interface_name).set(mixin_name);
}

void Context::register_typedef(ByteString const& name, Typedef typedef_, Module const& module)
{
    typedefs.set(name, move(typedef_));
    register_declaration(name, DeclarationKind::Typedef, module);
}

void Context::register_callback_function(ByteString const& name, CallbackFunction callback_function, Module const& module)
{
    callback_functions.set(name, move(callback_function));
    register_declaration(name, DeclarationKind::CallbackFunction, module);
}

Optional<Dictionary&> Context::get_dictionary(ByteString const& name)
{
    if (auto dictionary = dictionaries.get(name); dictionary.has_value())
        return dictionary.value();
    return {};
}

Optional<Dictionary const&> Context::get_dictionary(ByteString const& name) const
{
    if (auto dictionary = dictionaries.get(name); dictionary.has_value())
        return dictionary.value();
    return {};
}

Optional<Vector<Dictionary>&> Context::get_partial_dictionaries(ByteString const& name)
{
    if (auto dictionary = partial_dictionaries.get(name); dictionary.has_value())
        return dictionary.value();
    return {};
}

Optional<Vector<Dictionary> const&> Context::get_partial_dictionaries(ByteString const& name) const
{
    if (auto dictionary = partial_dictionaries.get(name); dictionary.has_value())
        return dictionary.value();
    return {};
}

Optional<Enumeration const&> Context::get_enumeration(ByteString const& name) const
{
    if (auto enumeration = enumerations.get(name); enumeration.has_value())
        return enumeration.value();
    return {};
}

Optional<Typedef const&> Context::get_typedef(ByteString const& name) const
{
    if (auto typedef_ = typedefs.get(name); typedef_.has_value())
        return typedef_.value();
    return {};
}

Optional<CallbackFunction const&> Context::get_callback_function(ByteString const& name) const
{
    if (auto callback_function = callback_functions.get(name); callback_function.has_value())
        return callback_function.value();
    return {};
}

Optional<Interface&> Context::get_mixin(ByteString const& name) const
{
    if (auto mixin = mixins.get(name); mixin.has_value())
        return *mixin.value();
    return {};
}

Optional<Vector<Interface const*> const&> Context::get_partial_interfaces(ByteString const& name) const
{
    if (auto interfaces = partial_interfaces.get(name); interfaces.has_value())
        return interfaces.value();
    return {};
}

Optional<Vector<Interface const*> const&> Context::get_partial_namespaces(ByteString const& name) const
{
    if (auto interfaces = partial_namespaces.get(name); interfaces.has_value())
        return interfaces.value();
    return {};
}

Optional<Vector<Interface const*> const&> Context::get_partial_mixins(ByteString const& name) const
{
    if (auto interfaces = partial_mixins.get(name); interfaces.has_value())
        return interfaces.value();
    return {};
}

Optional<HashTable<ByteString> const&> Context::get_included_mixins(ByteString const& name) const
{
    if (auto mixin_set = included_mixins.get(name); mixin_set.has_value())
        return mixin_set.value();
    return {};
}

bool Context::module_owns_declaration(Module const& module, ByteString const& name, DeclarationKind kind) const
{
    if (auto declaration_list = declarations.get(name); declaration_list.has_value()) {
        return any_of(*declaration_list, [&](auto const& declaration) {
            return declaration.owner == &module && declaration.kind == kind;
        });
    }
    return false;
}

bool Context::module_has_original_declaration(Module const& module, DeclarationKind kind) const
{
    for (auto const& declaration_list : declarations) {
        if (any_of(declaration_list.value, [&](auto const& declaration) {
                return declaration.owner == &module && declaration.kind == kind && declaration.is_original_definition;
            }))
            return true;
    }
    return false;
}

bool Context::module_will_generate_code(Module const& module) const
{
    if (module.primary_interface && !module.primary_interface->name.is_empty())
        return true;

    return module_has_original_declaration(module, DeclarationKind::Dictionary)
        || module_has_original_declaration(module, DeclarationKind::Enumeration);
}

Vector<Declaration const*> Context::declarations_for(Module const& module, DeclarationKind kind, DeclarationFilter filter) const
{
    Vector<Declaration const*> result;
    for (auto const& declaration_list : declarations) {
        for (auto const& declaration : declaration_list.value) {
            if (declaration.owner != &module)
                continue;
            if (declaration.kind != kind)
                continue;
            if (filter == DeclarationFilter::OriginalOnly && !declaration.is_original_definition)
                continue;
            result.append(&declaration);
        }
    }
    return result;
}

Optional<Module const&> Context::first_original_declaration_owner(ByteString const& name, DeclarationKind kind) const
{
    auto declaration_list = declarations.get(name);
    if (!declaration_list.has_value())
        return {};

    for (auto const& declaration : *declaration_list) {
        if (declaration.kind == kind && declaration.is_original_definition)
            return *declaration.owner;
    }
    return {};
}

void Context::resolve_all_types()
{
    for (auto const& module : owned_modules) {
        if (module->primary_interface)
            resolve_types(*module->primary_interface, true);
        for (auto const& mixin : mixins) {
            if (module_owns_declaration(*module, mixin.key, DeclarationKind::Mixin))
                resolve_types(*mixin.value, true);
        }
    }
}

void Context::resolve_types(Interface& interface, bool resolve_owned_definitions)
{
    if (interface.are_types_resolved)
        return;

    VERIFY(!interface.is_resolving_types);
    interface.is_resolving_types = true;
    auto const& module = module_for(interface);

    if (!interface.is_partial && interface.is_mixin) {
        if (auto partials = get_partial_mixins(interface.name); partials.has_value()) {
            for (auto const* partial_mixin : *partials)
                resolve_types(*const_cast<Interface*>(partial_mixin), false);
        }
    } else if (!interface.is_partial && interface.is_namespace) {
        if (auto partials = get_partial_namespaces(interface.name); partials.has_value()) {
            for (auto const* partial_namespace : *partials)
                resolve_types(*const_cast<Interface*>(partial_namespace), false);
        }
    } else if (!interface.is_partial && !interface.name.is_empty()) {
        if (auto partials = get_partial_interfaces(interface.name); partials.has_value()) {
            for (auto const* partial_interface : *partials)
                resolve_types(*const_cast<Interface*>(partial_interface), false);
        }
    }
    for (auto& attribute : interface.attributes)
        resolve_typedef(interface, attribute.type, &attribute.extended_attributes);
    for (auto& attribute : interface.static_attributes)
        resolve_typedef(interface, attribute.type, &attribute.extended_attributes);
    for (auto& constant : interface.constants)
        resolve_typedef(interface, constant.type);
    for (auto& constructor : interface.constructors)
        resolve_parameters_typedefs(interface, constructor.parameters);
    for (auto& function : interface.functions)
        resolve_function_typedefs(interface, function);
    for (auto& static_function : interface.static_functions)
        resolve_function_typedefs(interface, static_function);
    if (interface.value_iterator_type.has_value())
        resolve_typedef(interface, *interface.value_iterator_type);
    if (interface.pair_iterator_types.has_value()) {
        resolve_typedef(interface, interface.pair_iterator_types->get<0>());
        resolve_typedef(interface, interface.pair_iterator_types->get<1>());
    }
    if (interface.async_value_iterator_type.has_value())
        resolve_typedef(interface, *interface.async_value_iterator_type);
    resolve_parameters_typedefs(interface, interface.async_value_iterator_parameters);
    if (interface.set_entry_type.has_value())
        resolve_typedef(interface, *interface.set_entry_type);
    if (interface.map_key_type.has_value())
        resolve_typedef(interface, *interface.map_key_type);
    if (interface.map_value_type.has_value())
        resolve_typedef(interface, *interface.map_value_type);
    if (interface.named_property_getter.has_value())
        resolve_function_typedefs(interface, *interface.named_property_getter);
    if (interface.named_property_setter.has_value())
        resolve_function_typedefs(interface, *interface.named_property_setter);
    if (interface.indexed_property_getter.has_value())
        resolve_function_typedefs(interface, *interface.indexed_property_getter);
    if (interface.indexed_property_setter.has_value())
        resolve_function_typedefs(interface, *interface.indexed_property_setter);
    if (interface.named_property_deleter.has_value())
        resolve_function_typedefs(interface, *interface.named_property_deleter);
    if (resolve_owned_definitions) {
        for (auto const& mixin : mixins) {
            if (mixin.value == &interface)
                continue;
            if (module_owns_declaration(module, mixin.key, DeclarationKind::Mixin))
                resolve_types(*mixin.value, false);
        }

        for (auto& dictionary : dictionaries) {
            if (module_owns_declaration(module, dictionary.key, DeclarationKind::Dictionary)) {
                for (auto& dictionary_member : dictionary.value.members)
                    resolve_typedef(interface, dictionary_member.type, &dictionary_member.extended_attributes);
            }
        }
        for (auto& partial_dictionary_list : partial_dictionaries) {
            auto declaration_list = declarations.get(partial_dictionary_list.key);
            VERIFY(declaration_list.has_value());
            size_t partial_dictionary_index = 0;
            for (auto const& declaration : *declaration_list) {
                if (declaration.kind != DeclarationKind::PartialDictionary)
                    continue;
                VERIFY(partial_dictionary_index < partial_dictionary_list.value.size());
                auto& dictionary = partial_dictionary_list.value[partial_dictionary_index++];
                if (declaration.owner != &module)
                    continue;
                for (auto& dictionary_member : dictionary.members)
                    resolve_typedef(interface, dictionary_member.type, &dictionary_member.extended_attributes);
            }
            VERIFY(partial_dictionary_index == partial_dictionary_list.value.size());
        }
        for (auto& callback_function : callback_functions) {
            if (module_owns_declaration(module, callback_function.key, DeclarationKind::CallbackFunction))
                resolve_function_typedefs(interface, callback_function.value);
        }
    }

    rebuild_overload_sets(interface);

    interface.is_resolving_types = false;
    interface.are_types_resolved = true;
}

void Context::finalize_all_interfaces()
{
    for (auto const& module : owned_modules) {
        if (module->primary_interface)
            finalize_interface(*module->primary_interface);
    }
}

void Context::finalize_interface(Interface& interface)
{
    if (interface.is_finalized)
        return;

    VERIFY(!interface.is_finalizing);
    interface.is_finalizing = true;

    if (interface.is_mixin) {
        if (auto partials = get_partial_mixins(interface.name); partials.has_value()) {
            for (auto const* partial_mixin : *partials) {
                if (!should_apply_partial(interface, *partial_mixin))
                    continue;
                interface.extend_with_partial_interface(*partial_mixin);
            }
        }
    } else if (interface.is_namespace) {
        if (auto partials = get_partial_namespaces(interface.name); partials.has_value()) {
            for (auto const* partial_namespace : *partials) {
                if (!should_apply_partial(interface, *partial_namespace))
                    continue;
                interface.extend_with_partial_interface(*partial_namespace);
            }
        }
    } else if (!interface.name.is_empty()) {
        if (auto partials = get_partial_interfaces(interface.name); partials.has_value()) {
            for (auto const* partial_interface : *partials) {
                if (!should_apply_partial(interface, *partial_interface))
                    continue;
                interface.extend_with_partial_interface(*partial_interface);
            }
        }
    }

    if (auto mixin_names = get_included_mixins(interface.name); mixin_names.has_value()) {
        for (auto const& mixin_name : *mixin_names) {
            auto mixin = get_mixin(mixin_name);
            VERIFY(mixin.has_value());
            resolve_types(*mixin, true);
            finalize_interface(*mixin);

            for (auto const& attribute : mixin->attributes) {
                if (!any_of(interface.attributes, [&](auto const& existing_attribute) { return attributes_match(existing_attribute, attribute); }))
                    interface.attributes.append(attribute);
            }
            for (auto const& constant : mixin->constants) {
                if (!any_of(interface.constants, [&](auto const& existing_constant) { return constants_match(existing_constant, constant); }))
                    interface.constants.append(constant);
            }
            for (auto const& function : mixin->functions) {
                if (!any_of(interface.functions, [&](auto const& existing_function) { return functions_match(existing_function, function); }))
                    interface.functions.append(function);
            }
            for (auto const& function : mixin->static_functions) {
                if (!any_of(interface.static_functions, [&](auto const& existing_function) { return functions_match(existing_function, function); }))
                    interface.static_functions.append(function);
            }
            if (interface.has_stringifier && mixin->has_stringifier)
                VERIFY_NOT_REACHED();

            if (mixin->has_stringifier) {
                interface.stringifier_attribute = mixin->stringifier_attribute;
                interface.has_stringifier = true;
            }

            if (mixin->has_unscopable_member)
                interface.has_unscopable_member = true;
        }
    }

    rebuild_overload_sets(interface);

    interface.is_finalizing = false;
    interface.is_finalized = true;
}

// https://webidl.spec.whatwg.org/#dfn-platform-object
bool Context::is_platform_object(ByteString const& name) const
{
    // Platform objects are objects that implement an interface.
    // NB: WindowProxy is a special case as it is not defined over IDL, but implements the Window interface.
    return interfaces.contains(name) || name == "WindowProxy"sv;
}

Optional<Interface const&> Context::get_callback_interface(ByteString const& name) const
{
    if (auto interface = interfaces.get(name); interface.has_value() && interface.value()->is_callback_interface)
        return *interface.value();
    return {};
}

Optional<Interface const&> Context::get_interface(ByteString const& name) const
{
    if (auto interface = interfaces.get(name); interface.has_value())
        return *interface.value();
    return {};
}

bool Context::interface_has_original_dictionaries(Interface const& interface) const
{
    return module_has_original_declaration(module_for(interface), DeclarationKind::Dictionary);
}

bool Context::interface_has_original_enumerations(Interface const& interface) const
{
    return module_has_original_declaration(module_for(interface), DeclarationKind::Enumeration);
}

bool Interface::will_generate_code() const
{
    return context->module_will_generate_code(context->module_for(*this));
}

void EffectiveOverloadSet::remove_all_other_entries()
{
    Vector<Item> new_items;
    new_items.append(m_items[*m_last_matching_item_index]);
    m_items = move(new_items);
}

void Interface::dump()
{
    dbgln("Attributes:");
    for (auto& attribute : attributes) {
        dbgln("  {}{}{}{} {}",
            attribute.inherit ? "inherit " : "",
            attribute.readonly ? "readonly " : "",
            attribute.type->name(),
            attribute.type->is_nullable() ? "?" : "",
            attribute.name);
    }

    dbgln("Functions:");
    for (auto& function : functions) {
        dbgln("  {}{} {}",
            function.return_type->name(),
            function.return_type->is_nullable() ? "?" : "",
            function.name);
        for (auto& parameter : function.parameters) {
            dbgln("    {}{} {}",
                parameter.type->name(),
                parameter.type->is_nullable() ? "?" : "",
                parameter.name);
        }
    }

    dbgln("Static Functions:");
    for (auto& function : static_functions) {
        dbgln("  static {}{} {}",
            function.return_type->name(),
            function.return_type->is_nullable() ? "?" : "",
            function.name);
        for (auto& parameter : function.parameters) {
            dbgln("    {}{} {}",
                parameter.type->name(),
                parameter.type->is_nullable() ? "?" : "",
                parameter.name);
        }
    }
}

void Interface::extend_with_partial_interface(Interface const& partial)
{
    for (auto const& attribute : partial.attributes) {
        auto attribute_copy = attribute;
        attribute_copy.extended_attributes.update(partial.extended_attributes);
        if (!any_of(attributes, [&](auto const& existing_attribute) { return attributes_match(existing_attribute, attribute_copy); }))
            attributes.append(move(attribute_copy));
    }

    for (auto const& static_attribute : partial.static_attributes) {
        auto static_attribute_copy = static_attribute;
        static_attribute_copy.extended_attributes.update(partial.extended_attributes);
        if (!any_of(static_attributes, [&](auto const& existing_attribute) { return attributes_match(existing_attribute, static_attribute_copy); }))
            static_attributes.append(move(static_attribute_copy));
    }

    for (auto const& constant : partial.constants) {
        if (!any_of(constants, [&](auto const& existing_constant) { return constants_match(existing_constant, constant); }))
            constants.append(constant);
    }

    for (auto const& function : partial.functions) {
        auto function_copy = function;
        function_copy.extended_attributes.update(partial.extended_attributes);
        if (!any_of(functions, [&](auto const& existing_function) { return functions_match(existing_function, function_copy); }))
            functions.append(move(function_copy));
    }

    for (auto const& static_function : partial.static_functions) {
        auto static_function_copy = static_function;
        static_function_copy.extended_attributes.update(partial.extended_attributes);
        if (!any_of(static_functions, [&](auto const& existing_function) { return functions_match(existing_function, static_function_copy); }))
            static_functions.append(move(static_function_copy));
    }
}

}
