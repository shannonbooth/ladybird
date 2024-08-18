/*
 * Copyright (c) 2024, Andreas Kling <kling@serenityos.org>
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

class ConservativeVectorBase {
public:
    virtual ReadonlySpan<FlatPtr> possible_values() const = 0;

protected:
    explicit ConservativeVectorBase(GC::Heap&);
    ~ConservativeVectorBase();

    ConservativeVectorBase& operator=(ConservativeVectorBase const&);

    GC::Heap* m_heap { nullptr };
    IntrusiveListNode<ConservativeVectorBase> m_list_node;

public:
    using List = IntrusiveList<&ConservativeVectorBase::m_list_node>;
};

template<typename T, size_t inline_capacity>
class ConservativeVector final
    : public ConservativeVectorBase
    , public Vector<T, inline_capacity> {

public:
    explicit ConservativeVector(GC::Heap& heap)
        : ConservativeVectorBase(heap)
    {
    }

    virtual ~ConservativeVector() = default;

    ConservativeVector(ConservativeVector const& other)
        : ConservativeVectorBase(*other.m_heap)
        , Vector<T, inline_capacity>(other)
    {
    }

    ConservativeVector(ConservativeVector&& other)
        : ConservativeVectorBase(*other.m_heap)
        , Vector<T, inline_capacity>(move(static_cast<Vector<T, inline_capacity>&>(other)))
    {
    }

    ConservativeVector& operator=(ConservativeVector const& other)
    {
        Vector<T, inline_capacity>::operator=(other);
        ConservativeVectorBase::operator=(other);
        return *this;
    }

    virtual ReadonlySpan<FlatPtr> possible_values() const override
    {
        return ReadonlySpan<FlatPtr> { reinterpret_cast<FlatPtr const*>(this->data()), this->size() };
    }
};

}
