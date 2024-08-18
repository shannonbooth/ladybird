/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGC/Cell.h>
#include <LibGC/Handle.h>
#include <LibGC/Heap.h>

namespace JS {

HandleImpl::HandleImpl(GC::Cell* cell, SourceLocation location)
    : m_cell(cell)
    , m_location(location)
{

    static_cast<GC::Heap&>(GC::HeapBlockBase::from_cell(m_cell)->heap()).did_create_handle({}, *this);
    //m_cell->heap().did_create_handle({}, *this);
}

HandleImpl::~HandleImpl()
{
    static_cast<GC::Heap&>(GC::HeapBlockBase::from_cell(m_cell)->heap()).did_destroy_handle({}, *this);
    //m_cell->heap().did_destroy_handle({}, *this);
}

}
