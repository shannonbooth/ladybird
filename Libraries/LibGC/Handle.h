/*
 * Copyright (c) 2020, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Badge.h>
#include <AK/IntrusiveList.h>
#include <AK/Noncopyable.h>
#include <AK/RefCounted.h>
#include <AK/RefPtr.h>
#include <AK/SourceLocation.h>
#include <LibGC/Forward.h>
#include <LibGC/Ptr.h>

namespace GC {

class HandleImpl : public RefCounted<HandleImpl> {
    AK_MAKE_NONCOPYABLE(HandleImpl);
    AK_MAKE_NONMOVABLE(HandleImpl);

public:
    ~HandleImpl();

    Cell* cell() { return m_cell; }
    Cell const* cell() const { return m_cell; }

    SourceLocation const& source_location() const { return m_location; }

private:
    template<class T>
    friend class Handle;

    explicit HandleImpl(Cell*, SourceLocation location);
    Ptr<Cell> m_cell;
    SourceLocation m_location;

    IntrusiveListNode<HandleImpl> m_list_node;

public:
    using List = IntrusiveList<&HandleImpl::m_list_node>;
};

template<class T>
class Handle {
public:
    Handle() = default;

    static Handle create(T* cell, SourceLocation location = SourceLocation::current())
    {
        return Handle(adopt_ref(*new HandleImpl(const_cast<RemoveConst<T>*>(cell), location)));
    }

    Handle(T* cell, SourceLocation location = SourceLocation::current())
    {
        if (cell)
            m_impl = adopt_ref(*new HandleImpl(cell, location));
    }

    Handle(T& cell, SourceLocation location = SourceLocation::current())
        : m_impl(adopt_ref(*new HandleImpl(&cell, location)))
    {
    }

    Handle(Ptr<T> cell, SourceLocation location = SourceLocation::current())
        : Handle(cell.ptr(), location)
    {
    }

    Handle(Ref<T> cell, SourceLocation location = SourceLocation::current())
        : Handle(*cell, location)
    {
    }

    T* cell() const
    {
        if (!m_impl)
            return nullptr;
        return static_cast<T*>(m_impl->cell());
    }

    T* ptr() const
    {
        return cell();
    }

    bool is_null() const
    {
        return m_impl.is_null();
    }

    T* operator->() const
    {
        return cell();
    }

    [[nodiscard]] T& operator*() const
    {
        return *cell();
    }

    bool operator!() const
    {
        return !cell();
    }
    operator bool() const
    {
        return cell();
    }

    operator T*() const { return cell(); }

private:
    explicit Handle(NonnullRefPtr<HandleImpl> impl)
        : m_impl(move(impl))
    {
    }

    RefPtr<HandleImpl> m_impl;
};

template<class T>
inline Handle<T> make_handle(T* cell, SourceLocation location = SourceLocation::current())
{
    if (!cell)
        return Handle<T> {};
    return Handle<T>::create(cell, location);
}

template<class T>
inline Handle<T> make_handle(T& cell, SourceLocation location = SourceLocation::current())
{
    return Handle<T>::create(&cell, location);
}

template<class T>
inline Handle<T> make_handle(Ptr<T> cell, SourceLocation location = SourceLocation::current())
{
    if (!cell)
        return Handle<T> {};
    return Handle<T>::create(cell.ptr(), location);
}

template<class T>
inline Handle<T> make_handle(Ref<T> cell, SourceLocation location = SourceLocation::current())
{
    return Handle<T>::create(cell.ptr(), location);
}

}

namespace AK {

template<typename T>
struct Traits<GC::Handle<T>> : public DefaultTraits<GC::Handle<T>> {
    static unsigned hash(GC::Handle<T> const& handle) { return Traits<T>::hash(handle); }
};

namespace Detail {
template<typename T>
inline constexpr bool IsHashCompatible<GC::Handle<T>, T> = true;

template<typename T>
inline constexpr bool IsHashCompatible<T, GC::Handle<T>> = true;

}
}
