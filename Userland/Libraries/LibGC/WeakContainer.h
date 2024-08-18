/*
 * Copyright (c) 2021, Idan Horowitz <idan.horowitz@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/IntrusiveList.h>
#include <LibGC/Forward.h>

namespace JS {

class WeakContainer {
public:
    explicit WeakContainer(GC::Heap&);
    virtual ~WeakContainer();

    virtual void remove_dead_cells(Badge<GC::Heap>) = 0;

protected:
    void deregister();

private:
    bool m_registered { true };
    GC::Heap& m_heap;

    IntrusiveListNode<WeakContainer> m_list_node;

public:
    using List = IntrusiveList<&WeakContainer::m_list_node>;
};

}
