/*
 * Copyright (c) 2020, the SerenityOS developers.
 * Copyright (c) 2024, Jamie Mansfield <jmansfield@cadixdev.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Forward.h>
#include <LibWeb/HTML/HTMLElement.h>
#include <LibWeb/WebIDL/Types.h>

namespace Web::HTML {

// NOTE: This element is marked as obsolete, but is still listed as required by the specification.
class HTMLMarqueeElement final : public HTMLElement {
    WEB_PLATFORM_OBJECT(HTMLMarqueeElement, HTMLElement);
    GC_DECLARE_ALLOCATOR(HTMLMarqueeElement);

public:
    virtual ~HTMLMarqueeElement() override;

    WebIDL::Long loop() const;
    void set_loop(WebIDL::Long);

    WebIDL::UnsignedLong scroll_amount() const;
    void set_scroll_amount(WebIDL::UnsignedLong);

    WebIDL::UnsignedLong scroll_delay() const;
    void set_scroll_delay(WebIDL::UnsignedLong);

private:
    HTMLMarqueeElement(DOM::Document&, DOM::QualifiedName);

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Cell::Visitor&) override;

    virtual bool is_presentational_hint(FlyString const&) const override;
    virtual void apply_presentational_hints(GC::Ref<CSS::CascadedProperties>) const override;

    virtual void inserted() override;
    virtual void removed_from(DOM::Node* old_parent, DOM::Node& old_root) override;

    virtual void attribute_changed(FlyString const& local_name, Optional<String> const& old_value, Optional<String> const& value, Optional<FlyString> const& namespace_) override;

    WebIDL::Long loop_count() const;

    enum class Behavior : u8 {
        Alternate,
        Scroll,
        Slide,
    };

    enum class Direction : u8 {
        Down,
        Left,
        Right,
        Up,
    };

    enum class Axis : bool {
        Horizontal,
        Vertical,
    };

    void create_shadow_tree_if_needed();
    void update_shadow_tree_style();
    void reset_animation_state();
    bool increment_current_loop_index();
    void schedule_animation_frame_if_needed();
    void cancel_animation_frame();
    void run_animation_step(double now);

    Behavior marquee_behavior() const;
    Direction marquee_direction() const;
    static Axis axis_for_direction(Direction);
    WebIDL::UnsignedLong effective_scroll_delay() const;

    GC::Ptr<Element> m_slot_element;

    Optional<WebIDL::UnsignedLong> m_animation_frame_callback_id;
    Optional<double> m_last_animation_frame_timestamp;
    Optional<CSSPixels> m_current_offset;

    // https://html.spec.whatwg.org/multipage/obsolete.html#marquee-current-loop-index
    // A marquee element also has a marquee current loop index, which is zero when the element is created.
    u32 m_current_loop_index { 0 };

    bool m_moving_towards_end { true };
};

}
