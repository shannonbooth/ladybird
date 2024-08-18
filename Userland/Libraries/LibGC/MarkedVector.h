/*
 * Copyright (c) 2021, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2022, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/IntrusiveList.h>
#include <AK/Vector.h>
#include <LibGC/Forward.h>
#include <LibGC/Cell.h>
#include <LibGC/HeapRoot.h>

namespace JS {

class MarkedVectorBase {
public:
    virtual void gather_roots(HashMap<GC::Cell*, GC::HeapRoot>&) const = 0;

protected:
    explicit MarkedVectorBase(GC::Heap&);
    ~MarkedVectorBase();

    MarkedVectorBase& operator=(MarkedVectorBase const&);

    GC::Heap* m_heap { nullptr };
    IntrusiveListNode<MarkedVectorBase> m_list_node;

public:
    using List = IntrusiveList<&MarkedVectorBase::m_list_node>;
};

template<typename T, size_t inline_capacity>
class MarkedVector final
    : public MarkedVectorBase
    , public Vector<T, inline_capacity> {

public:
    explicit MarkedVector(GC::Heap& heap)
        : MarkedVectorBase(heap)
    {
    }

    virtual ~MarkedVector() = default;

    MarkedVector(MarkedVector const& other)
        : MarkedVectorBase(*other.m_heap)
        , Vector<T, inline_capacity>(other)
    {
    }

    MarkedVector(MarkedVector&& other)
        : MarkedVectorBase(*other.m_heap)
        , Vector<T, inline_capacity>(move(static_cast<Vector<T, inline_capacity>&>(other)))
    {
    }

    MarkedVector& operator=(MarkedVector const& other)
    {
        Vector<T, inline_capacity>::operator=(other);
        MarkedVectorBase::operator=(other);
        return *this;
    }

    virtual void gather_roots(HashMap<GC::Cell*, GC::HeapRoot>& roots) const override
    {
        for (auto& value : *this) {
            if constexpr (IsBaseOf<GC::NanBoxedValue, T>) {
                if (value.is_cell())
                    roots.set(&const_cast<T&>(value).as_cell(), GC::HeapRoot { .type = GC::HeapRoot::Type::MarkedVector });
            } else {
                roots.set(value, GC::HeapRoot { .type = GC::HeapRoot::Type::MarkedVector });
            }
        }
    }
};

}
