/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Noncopyable.h>
#include <AK/WeakPtr.h>

namespace WebView {

// Holds a tab for a frontend without a widget, such as a headless one. The traversable owns the tab's view, which may
// go away first when the traversable is destroyed.
template<typename T>
class TabHandle {
    AK_MAKE_NONCOPYABLE(TabHandle);

public:
    TabHandle() = default;

    explicit TabHandle(T& view)
        : m_view(view.template make_weak_ptr<T>())
    {
        view.add_frontend_hold();
    }

    TabHandle(TabHandle&& other)
        : m_view(move(other.m_view))
    {
        other.m_view = nullptr;
    }

    TabHandle& operator=(TabHandle&& other)
    {
        if (this != &other) {
            close();
            m_view = move(other.m_view);
            other.m_view = nullptr;
        }
        return *this;
    }

    ~TabHandle() { close(); }

    void close()
    {
        if (auto* view = m_view.ptr()) {
            m_view = nullptr;
            view->release_frontend_hold();
        }
    }

    T* ptr() const { return m_view.ptr(); }
    T* operator->() const
    {
        VERIFY(m_view);
        return m_view.ptr();
    }
    T& operator*() const
    {
        VERIFY(m_view);
        return *m_view;
    }
    explicit operator bool() const { return static_cast<bool>(m_view); }

private:
    WeakPtr<T> m_view;
};

}
