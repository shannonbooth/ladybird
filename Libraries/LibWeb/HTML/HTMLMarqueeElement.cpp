/*
 * Copyright (c) 2020, the SerenityOS developers.
 * Copyright (c) 2024, Jamie Mansfield <jmansfield@cadixdev.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/HTMLMarqueeElementPrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/CSS/CSSStyleProperties.h>
#include <LibWeb/CSS/CascadedProperties.h>
#include <LibWeb/CSS/ComputedProperties.h>
#include <LibWeb/CSS/StyleValues/ColorStyleValue.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/ElementFactory.h>
#include <LibWeb/DOM/ShadowRoot.h>
#include <LibWeb/HTML/AnimationFrameCallbackDriver.h>
#include <LibWeb/HTML/HTMLMarqueeElement.h>
#include <LibWeb/HTML/HTMLSlotElement.h>
#include <LibWeb/HTML/Numbers.h>
#include <LibWeb/HTML/Parser/HTMLParser.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/Namespace.h>
#include <LibWeb/Painting/PaintableBox.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(HTMLMarqueeElement);

HTMLMarqueeElement::HTMLMarqueeElement(DOM::Document& document, DOM::QualifiedName qualified_name)
    : HTMLElement(document, move(qualified_name))
{
}

HTMLMarqueeElement::~HTMLMarqueeElement() = default;

void HTMLMarqueeElement::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(HTMLMarqueeElement);
    Base::initialize(realm);
}

void HTMLMarqueeElement::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_slot_element);
}

void HTMLMarqueeElement::inserted()
{
    Base::inserted();
    create_shadow_tree_if_needed();
    reset_animation_state();
    schedule_animation_frame_if_needed();
}

void HTMLMarqueeElement::removed_from(DOM::Node* old_parent, DOM::Node& old_root)
{
    cancel_animation_frame();
    Base::removed_from(old_parent, old_root);
}

bool HTMLMarqueeElement::is_presentational_hint(FlyString const& name) const
{
    if (Base::is_presentational_hint(name))
        return true;

    return first_is_one_of(name,
        HTML::AttributeNames::bgcolor,
        HTML::AttributeNames::height,
        HTML::AttributeNames::hspace,
        HTML::AttributeNames::vspace,
        HTML::AttributeNames::width);
}

void HTMLMarqueeElement::create_shadow_tree_if_needed()
{
    if (shadow_root())
        return;

    auto& shadow_root = *realm().create<DOM::ShadowRoot>(document(), *this, Bindings::ShadowRootMode::Closed);
    shadow_root.set_user_agent_internal(true);
    set_shadow_root(shadow_root);

    m_slot_element = MUST(DOM::create_element(document(), HTML::TagNames::slot, Namespace::HTML));
    MUST(shadow_root.append_child(*m_slot_element));

    update_shadow_tree_style();
}

void HTMLMarqueeElement::apply_presentational_hints(GC::Ref<CSS::CascadedProperties> cascaded_properties) const
{
    HTMLElement::apply_presentational_hints(cascaded_properties);
    for_each_attribute([&](auto& name, auto& value) {
        if (name == HTML::AttributeNames::bgcolor) {
            // https://html.spec.whatwg.org/multipage/rendering.html#the-marquee-element-2:rules-for-parsing-a-legacy-colour-value
            auto color = parse_legacy_color_value(value);
            if (color.has_value())
                cascaded_properties->set_property_from_presentational_hint(CSS::PropertyID::BackgroundColor, CSS::ColorStyleValue::create_from_color(color.value(), CSS::ColorSyntax::Legacy));
        } else if (name == HTML::AttributeNames::height) {
            // https://html.spec.whatwg.org/multipage/rendering.html#the-marquee-element-2:maps-to-the-dimension-property
            if (auto parsed_value = parse_dimension_value(value)) {
                cascaded_properties->set_property_from_presentational_hint(CSS::PropertyID::Height, *parsed_value);
            }
        } else if (name == HTML::AttributeNames::hspace) {
            if (auto parsed_value = parse_dimension_value(value)) {
                cascaded_properties->set_property_from_presentational_hint(CSS::PropertyID::MarginLeft, *parsed_value);
                cascaded_properties->set_property_from_presentational_hint(CSS::PropertyID::MarginRight, *parsed_value);
            }
        } else if (name == HTML::AttributeNames::vspace) {
            if (auto parsed_value = parse_dimension_value(value)) {
                cascaded_properties->set_property_from_presentational_hint(CSS::PropertyID::MarginTop, *parsed_value);
                cascaded_properties->set_property_from_presentational_hint(CSS::PropertyID::MarginBottom, *parsed_value);
            }
        } else if (name == HTML::AttributeNames::width) {
            if (auto parsed_value = parse_dimension_value(value)) {
                cascaded_properties->set_property_from_presentational_hint(CSS::PropertyID::Width, *parsed_value);
            }
        }
    });
}

void HTMLMarqueeElement::attribute_changed(FlyString const& local_name, Optional<String> const& old_value, Optional<String> const& value, Optional<FlyString> const& namespace_)
{
    Base::attribute_changed(local_name, old_value, value, namespace_);

    if (namespace_.has_value())
        return;

    if (local_name == HTML::AttributeNames::direction) {
        // Direction affects layout axis; update styles and restart from the beginning.
        update_shadow_tree_style();
        reset_animation_state();
        schedule_animation_frame_if_needed();
    } else if (local_name == HTML::AttributeNames::behavior) {
        // Behavior affects the animation path; restart from the beginning.
        reset_animation_state();
        schedule_animation_frame_if_needed();
    } else if (local_name == HTML::AttributeNames::loop) {
        // Reset loop index and reschedule in case the animation was stopped due to loop exhaustion.
        m_current_loop_index = 0;
        schedule_animation_frame_if_needed();
    }
    // scrollamount, scrolldelay, truespeed are read dynamically each frame; no restart needed.
}

// https://html.spec.whatwg.org/multipage/obsolete.html#dom-marquee-loop
WebIDL::Long HTMLMarqueeElement::loop() const
{
    // The loop IDL attribute, on getting, must return the element's marquee loop count;
    return loop_count();
}

void HTMLMarqueeElement::set_loop(WebIDL::Long count)
{
    // and on setting, if the new value is different than the element's marquee loop count and either greater than zero
    // or equal to −1, must set the element's loop content attribute (adding it if necessary) to the valid integer that
    // represents the new value. (Other values are ignored.)
    if (count != loop_count() && (count > 0 || count == -1))
        set_attribute_value(HTML::AttributeNames::loop, String::number(count));
}

// https://html.spec.whatwg.org/multipage/obsolete.html#dom-marquee-scrollamount
WebIDL::UnsignedLong HTMLMarqueeElement::scroll_amount() const
{
    // The scrollAmount IDL attribute must reflect the scrollamount content attribute. The default value is 6.
    if (auto scroll_amount_string = get_attribute(HTML::AttributeNames::scrollamount); scroll_amount_string.has_value()) {
        if (auto scroll_amount = parse_non_negative_integer(*scroll_amount_string); scroll_amount.has_value() && *scroll_amount <= 2147483647)
            return *scroll_amount;
    }
    return 6;
}

// https://html.spec.whatwg.org/multipage/obsolete.html#dom-marquee-scrollamount
void HTMLMarqueeElement::set_scroll_amount(WebIDL::UnsignedLong value)
{
    if (value > 2147483647)
        value = 6;
    set_attribute_value(HTML::AttributeNames::scrollamount, String::number(value));
}

// https://html.spec.whatwg.org/multipage/obsolete.html#dom-marquee-scrolldelay
WebIDL::UnsignedLong HTMLMarqueeElement::scroll_delay() const
{
    // The scrollDelay IDL attribute must reflect the scrolldelay content attribute. The default value is 85.
    if (auto scroll_delay_string = get_attribute(HTML::AttributeNames::scrolldelay); scroll_delay_string.has_value()) {
        if (auto scroll_delay = parse_non_negative_integer(*scroll_delay_string); scroll_delay.has_value() && *scroll_delay <= 2147483647)
            return *scroll_delay;
    }
    return 85;
}

// https://html.spec.whatwg.org/multipage/obsolete.html#dom-marquee-scrolldelay
void HTMLMarqueeElement::set_scroll_delay(WebIDL::UnsignedLong value)
{
    if (value > 2147483647)
        value = 85;
    set_attribute_value(HTML::AttributeNames::scrolldelay, String::number(value));
}

// https://html.spec.whatwg.org/multipage/obsolete.html#marquee-scroll-interval
WebIDL::UnsignedLong HTMLMarqueeElement::effective_scroll_delay() const
{
    // 1. If the element has a scrolldelay attribute, and parsing its value using the rules for parsing non-negative
    //    integers does not return an error, then let delay be the parsed value. Otherwise, let delay be 85.
    auto delay = scroll_delay();

    // 2. If the element does not have a truespeed attribute, and the delay value is less than 60, then let delay be 60 instead.
    if (!has_attribute(HTML::AttributeNames::truespeed) && delay < 60)
        delay = 60;

    // 3. The marquee scroll interval is delay, interpreted in milliseconds.
    return delay;
}

// https://html.spec.whatwg.org/multipage/obsolete.html#marquee-loop-count
WebIDL::Long HTMLMarqueeElement::loop_count() const
{
    // A marquee element has a marquee loop count, which, if the element has a loop attribute, and parsing its value using
    // the rules for parsing integers does not return an error or a number less than 1, is the parsed value, and otherwise is −1.
    if (auto loop = get_attribute(HTML::AttributeNames::loop); loop.has_value()) {
        if (auto parsed_loop = parse_integer(*loop); parsed_loop.has_value() && *parsed_loop > 0)
            return *parsed_loop;
    }

    return -1;
}

void HTMLMarqueeElement::reset_animation_state()
{
    m_current_offset.clear();
    m_last_animation_frame_timestamp.clear();
    m_current_loop_index = 0;
    m_moving_towards_end = true;
}

// https://html.spec.whatwg.org/multipage/obsolete.html#increment-the-marquee-current-loop-index
bool HTMLMarqueeElement::increment_current_loop_index()
{
    auto loop_count = this->loop_count();

    // 1. If the marquee loop count is −1, then return.
    if (loop_count == -1)
        return true;

    // 2. Increment the marquee current loop index by one.
    ++m_current_loop_index;

    // 3. If the marquee current loop index is now greater than or equal to the element's marquee loop count, turn off
    //    the marquee element.
    return m_current_loop_index < static_cast<u32>(loop_count);
}

void HTMLMarqueeElement::schedule_animation_frame_if_needed()
{
    if (m_animation_frame_callback_id.has_value())
        return;

    m_animation_frame_callback_id = document().window()->animation_frame_callback_driver().add(
        GC::create_function(heap(), [this](double now) {
            run_animation_step(now);
        }));
}

void HTMLMarqueeElement::cancel_animation_frame()
{
    if (!m_animation_frame_callback_id.has_value())
        return;

    document().window()->animation_frame_callback_driver().remove(*m_animation_frame_callback_id);
    m_animation_frame_callback_id.clear();
}

void HTMLMarqueeElement::update_shadow_tree_style()
{
    create_shadow_tree_if_needed();
    auto const axis = axis_for_direction(marquee_direction());
    MUST(m_slot_element->style_for_bindings()->set_property(CSS::PropertyID::Display, "block"sv));
    MUST(m_slot_element->style_for_bindings()->set_property(CSS::PropertyID::WillChange, "translate"sv));
    MUST(m_slot_element->style_for_bindings()->set_property(CSS::PropertyID::WhiteSpace, axis == Axis::Horizontal ? "nowrap"sv : "normal"sv));
}

struct OffsetRange {
    CSSPixels start;
    CSSPixels end;
};

void HTMLMarqueeElement::run_animation_step(double now)
{
    m_animation_frame_callback_id.clear();

    if (!m_slot_element)
        return;

    auto const direction = marquee_direction();
    auto const axis = axis_for_direction(direction);

    document().update_layout_if_needed_for_node(*this, DOM::UpdateLayoutReason::HTMLMarqueeElementAnimation);
    auto* marquee_box = paintable_box();
    auto* slot_box = m_slot_element->paintable_box();
    VERIFY(marquee_box);
    VERIFY(slot_box);

    auto const host_extent = axis == Axis::Horizontal ? marquee_box->absolute_padding_box_rect().width() : marquee_box->absolute_padding_box_rect().height();
    auto const content_extent = axis == Axis::Horizontal ? slot_box->absolute_united_border_box_rect().width() : slot_box->absolute_united_border_box_rect().height();
    auto const max_offset = host_extent + content_extent;

    // Compute the animation path [start, end] for the current behavior and direction.
    // current_offset represents how far the content has traveled along the path.
    // visual_offset = host_extent - current_offset places the slot such that:
    //   - at path.start the content is at its entry position (just off the entering edge)
    //   - at path.end the content is at its exit/rest position
    OffsetRange path {};
    switch (marquee_behavior()) {
    case Behavior::Scroll:
        // Content enters from one edge and exits the other, continuously.
        path = direction == Direction::Left || direction == Direction::Up
            ? OffsetRange { 0, max_offset }
            : OffsetRange { max_offset, 0 };
        break;
    case Behavior::Slide:
        // Content enters from one edge and comes to rest at the opposite edge.
        path = direction == Direction::Left || direction == Direction::Up
            ? OffsetRange { 0, host_extent }
            : OffsetRange { max_offset, content_extent };
        break;
    case Behavior::Alternate:
        // Content bounces between the two edges.
        path = direction == Direction::Left || direction == Direction::Up
            ? OffsetRange { content_extent, host_extent }
            : OffsetRange { host_extent, content_extent };
        break;
    }

    if (!m_current_offset.has_value())
        m_current_offset = path.start;
    else
        *m_current_offset = clamp(*m_current_offset, min(path.start, path.end), max(path.start, path.end));

    auto const apply_offset = [&]() {
        auto const visual_offset = host_extent - *m_current_offset;
        auto const transform = axis == Axis::Horizontal
            ? MUST(String::formatted("translateX({}px)", visual_offset))
            : MUST(String::formatted("translateY({}px)", visual_offset));
        MUST(m_slot_element->style_for_bindings()->set_property(CSS::PropertyID::Transform, transform));
    };

    auto const pixels_per_ms = CSSPixels { scroll_amount() } / max(effective_scroll_delay(), 1u);
    if (path.start == path.end || pixels_per_ms <= 0) {
        apply_offset();
        m_last_animation_frame_timestamp.clear();
        return;
    }

    auto const elapsed = m_last_animation_frame_timestamp.has_value() ? now - *m_last_animation_frame_timestamp : 0.0;
    auto const elapsed_distance = CSSPixels::nearest_value_for(pixels_per_ms * max(0.0, elapsed));
    auto const target_offset = m_moving_towards_end ? path.end : path.start;
    auto const distance_to_target = target_offset - *m_current_offset;
    auto const target_distance = abs(distance_to_target);

    if (target_distance > elapsed_distance) {
        // Still traveling toward the target edge; advance by the elapsed distance.
        *m_current_offset += distance_to_target > 0 ? elapsed_distance : -elapsed_distance;
    } else {
        // Reached (or passed) the target edge this frame.
        *m_current_offset = target_offset;

        if (!increment_current_loop_index()) {
            apply_offset();
            m_last_animation_frame_timestamp.clear();
            return;
        }

        if (marquee_behavior() == Behavior::Alternate)
            m_moving_towards_end = !m_moving_towards_end;
        else
            *m_current_offset = path.start;
    }

    apply_offset();
    m_last_animation_frame_timestamp = now;
    schedule_animation_frame_if_needed();
}

HTMLMarqueeElement::Behavior HTMLMarqueeElement::marquee_behavior() const
{
    if (auto behavior = get_attribute(HTML::AttributeNames::behavior); behavior.has_value()) {
        if (behavior->equals_ignoring_ascii_case("alternate"sv))
            return Behavior::Alternate;
        if (behavior->equals_ignoring_ascii_case("slide"sv))
            return Behavior::Slide;
    }
    return Behavior::Scroll;
}

HTMLMarqueeElement::Direction HTMLMarqueeElement::marquee_direction() const
{
    if (auto direction = get_attribute(HTML::AttributeNames::direction); direction.has_value()) {
        if (direction->equals_ignoring_ascii_case("right"sv))
            return Direction::Right;
        if (direction->equals_ignoring_ascii_case("up"sv))
            return Direction::Up;
        if (direction->equals_ignoring_ascii_case("down"sv))
            return Direction::Down;
    }
    return Direction::Left;
}

HTMLMarqueeElement::Axis HTMLMarqueeElement::axis_for_direction(Direction direction)
{
    return direction == Direction::Left || direction == Direction::Right ? Axis::Horizontal : Axis::Vertical;
}

}
