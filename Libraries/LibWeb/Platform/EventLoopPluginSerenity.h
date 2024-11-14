/*
 * Copyright (c) 2022, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Platform/EventLoopPlugin.h>

namespace Web::Platform {

class EventLoopPluginSerenity final : public Web::Platform::EventLoopPlugin {
public:
    EventLoopPluginSerenity();
    virtual ~EventLoopPluginSerenity() override;

    virtual void spin_until(GC::Handle<GC::Function<bool()>> goal_condition) override;
    virtual void deferred_invoke(GC::Handle<GC::Function<void()>>) override;
    virtual GC::Ref<Timer> create_timer(JS::Heap&) override;
    virtual void quit() override;
};

}
