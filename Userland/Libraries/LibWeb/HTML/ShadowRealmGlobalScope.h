#pragma once

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/DOM/EventTarget.h>

namespace Web::HTML {

class ShadowRealmGlobalScope : public DOM::EventTarget {
    WEB_PLATFORM_OBJECT(ShadowRealmGlobalScope, DOM::EventTarget);
    JS_DECLARE_ALLOCATOR(ShadowRealmGlobalScope);

public:
    virtual ~ShadowRealmGlobalScope() override;

    static JS::NonnullGCPtr<ShadowRealmGlobalScope> create(JS::Realm&);

    JS::NonnullGCPtr<ShadowRealmGlobalScope> self() { return *this; }

protected:
    explicit ShadowRealmGlobalScope(JS::Realm&);

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Cell::Visitor&) override;
};

}
