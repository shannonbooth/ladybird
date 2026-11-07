/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/Timer.h>
#include <LibGC/Weak.h>
#include <LibWeb/Bindings/RustInterface.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/PermissionsAPI/Permissions.h>
#include <LibWeb/WebAudio/RustFFI.h>

namespace Web::WebAudio {

extern "C" RustFFI::FfiSharedCppObject web_audio_playback_stats_create_timer(Bindings::RustInterfaceCell*);
extern "C" bool web_audio_playback_stats_can_update(DOM::EventTarget*);
extern "C" void web_rust_audio_playback_stats_update(Bindings::RustInterfaceCell*);

RustFFI::FfiSharedCppObject web_audio_playback_stats_create_timer(Bindings::RustInterfaceCell* stats)
{
    auto timer = Core::Timer::create_repeating(AK::Duration::from_seconds(1), [stats = GC::Weak { *stats }] {
        if (auto cell = stats.ptr())
            web_rust_audio_playback_stats_update(cell.ptr());
    });
    timer->start();
    return {
        &timer.leak_ref(),
        [](void* timer) { static_cast<Core::Timer*>(timer)->ref(); },
        [](void* timer) { static_cast<Core::Timer*>(timer)->unref(); },
    };
}

bool web_audio_playback_stats_can_update(DOM::EventTarget* global)
{
    auto& window = as<HTML::Window>(*global);
    auto& document = window.associated_document();
    if (document.is_fully_active() && document.visibility_state() == HTML::VisibilityState::Visible)
        return true;

    return PermissionsAPI::permission_state({ "microphone"_utf16 }, HTML::relevant_settings_object(window)) == PermissionsAPI::PermissionState::Granted;
}

}
