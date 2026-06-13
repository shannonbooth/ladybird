/*
 * Copyright (c) 2021-2022, Linus Groh <linusg@serenityos.org>
 * Copyright (c) 2022, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/TypeCasts.h>
#include <LibGC/DeferGC.h>
#include <LibJS/Runtime/DeclarativeEnvironment.h>
#include <LibJS/Runtime/GlobalEnvironment.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/NativeFunction.h>
#include <LibJS/Runtime/Realm.h>
#include <LibJS/Runtime/VM.h>

namespace JS {

GC_DEFINE_ALLOCATOR(Realm);

// 9.3.1 InitializeHostDefinedRealm ( ), https://tc39.es/ecma262/#sec-initializehostdefinedrealm
NonnullOwnPtr<ExecutionContext> Realm::initialize_host_defined_realm(VM& vm, Function<GlobalAndThisValue(ExecutionContext&)> customizations)
{
    GC::DeferGC defer_gc(vm.heap());

    // 1. Let realm be a new Realm Record.
    auto realm = vm.heap().allocate<Realm>();

    // 2. Perform CreateIntrinsics(realm).
    Intrinsics::create(*realm);

    // 3. Let newContext be a new execution context.
    auto new_context = ExecutionContext::create(0, ReadonlySpan<Value> {}, 0);

    // 4. Set the Function of newContext to null.
    new_context->function = nullptr;

    // 5. Set the Realm of newContext to realm.
    new_context->realm = realm;

    // 6. Set the ScriptOrModule of newContext to null.
    new_context->script_or_module = {};

    // 7. Let global and thisValue be the elements of customizations(newContext).
    auto [global, this_value] = customizations(*new_context);

    // 8. If global is undefined, then
    if (!global) {
        // 1. Set global to OrdinaryObjectCreate(_realm_.[[Intrinsics]].[[%Object.prototype%]]).
        // NB: We allocate a proper GlobalObject directly as this plain object is
        //     turned into one via SetDefaultGlobalBindings in the spec.
        global = vm.heap().allocate<GlobalObject>(realm);
    }
    // 9. Else,
    else {
        // 1. Assert: global is an Object
        // NB: Known by the Type.
    }

    // 10. If thisValue is undefined, then
    if (!this_value) {
        // 1. Set thisValue to global.
        this_value = global;
    }
    // 11. Else,
    else {
        // 1. Assert: thisValue is an Object.
    }

    // 12. Set realm.[[GlobalObject]] to global.
    realm->m_global_object = global;

    // 13. Set realm.[[GlobalEnv]] to NewGlobalEnvironment(global, thisValue).
    realm->set_global_environment(vm.heap().allocate<GlobalEnvironment>(*global, *this_value));

    // 14. Perform SetDefaultGlobalBindings(realm).
    set_default_global_bindings(*realm);

    // 15. Return newContext.
    return new_context;
}

void Realm::set_global_environment(GC::Ref<GlobalEnvironment> environment)
{
    m_global_environment = environment;
    m_global_declarative_environment = &environment->declarative_record();
}

void Realm::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_intrinsics);
    visitor.visit(m_global_object);
    visitor.visit(m_global_environment);
    visitor.visit(m_global_declarative_environment);
    if (m_host_defined)
        m_host_defined->visit_edges(visitor);
}

}
