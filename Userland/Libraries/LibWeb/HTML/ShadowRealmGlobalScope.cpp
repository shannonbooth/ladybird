#include <LibWeb/Bindings/ShadowRealmGlobalScopePrototype.h>
#include <LibWeb/HTML/ShadowRealmGlobalScope.h>

namespace Web::HTML {

JS_DEFINE_ALLOCATOR(ShadowRealmGlobalScope);

ShadowRealmGlobalScope::ShadowRealmGlobalScope(JS::Realm& realm)
    : DOM::EventTarget(realm)
{
}

ShadowRealmGlobalScope::~ShadowRealmGlobalScope() = default;

JS::NonnullGCPtr<ShadowRealmGlobalScope> ShadowRealmGlobalScope::create(JS::Realm& realm)
{
    return realm.heap().allocate<ShadowRealmGlobalScope>(realm, realm);
}

void ShadowRealmGlobalScope::initialize(JS::Realm&)
{
    // Maybe needs to be commented out.
    // Base::initialize(realm);
    // WEB_SET_PROTOTYPE_FOR_INTERFACE(ShadowRealmGlobalScope);
}

void ShadowRealmGlobalScope::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
}

}
