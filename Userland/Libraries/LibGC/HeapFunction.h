/*
 * Copyright (c) 2023, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <LibGC/Cell.h>
#include <LibGC/Heap.h>

namespace JS {

template<typename T>
class HeapFunction final : public GC::Cell {
    GC_CELL(HeapFunction, GC::Cell);

public:
    static NonnullGCPtr<HeapFunction> create(GC::Heap& heap, Function<T> function)
    {
        return heap.allocate_without_impl<HeapFunction>(move(function)); // FIXME
    }

    virtual ~HeapFunction() override = default;

    [[nodiscard]] Function<T> const& function() const { return m_function; }

private:
    HeapFunction(Function<T> function)
        : m_function(move(function))
    {
    }

    virtual void visit_edges(Visitor& visitor) override
    {
        Base::visit_edges(visitor);
        visitor.visit_possible_values(m_function.raw_capture_range());
    }

    Function<T> m_function;
};

template<typename Callable, typename T = EquivalentFunctionType<Callable>>
static NonnullGCPtr<HeapFunction<T>> create_heap_function(GC::Heap& heap, Callable&& function)
{
    return HeapFunction<T>::create(heap, Function<T> { forward<Callable>(function) });
}

}
