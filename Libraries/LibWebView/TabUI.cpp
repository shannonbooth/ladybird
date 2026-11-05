/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/NeverDestroyed.h>
#include <LibWebCommon/Geolocation/GeolocationPositionErrorCode.h>
#include <LibWebView/TabUI.h>
#include <LibWebView/ViewImplementation.h>
#include <LibWebView/WebContentPage.h>

namespace WebView {

TabUI& TabUI::no_ui()
{
    static NeverDestroyed<TabUI> ui;
    return *ui;
}

void TabUserRequests::request_alert(Utf16String const&, NonnullRefPtr<DialogReply> reply)
{
    reply->cancel();
}

void TabUserRequests::request_confirm(Utf16String const&, NonnullRefPtr<DialogReply> reply)
{
    reply->cancel();
}

void TabUserRequests::request_prompt(Utf16String const&, Utf16String const&, NonnullRefPtr<DialogReply> reply)
{
    reply->cancel();
}

void TabUserRequests::request_before_unload(String const&, NonnullRefPtr<DialogReply> reply)
{
    reply->cancel();
}

void TabUserRequests::request_color_picker(Color, NonnullRefPtr<ColorPickerReply> reply)
{
    reply->cancel();
}

void TabUserRequests::request_date_picker(Utf16String const&, Utf16String const&, Utf16String const&, Utf16String const&, NonnullRefPtr<DatePickerReply> reply)
{
    reply->cancel();
}

void TabUserRequests::request_file_picker(Web::HTML::FileFilter const&, Web::HTML::AllowMultipleFiles, NonnullRefPtr<FilePickerReply> reply)
{
    reply->cancel();
}

void TabUserRequests::request_select_dropdown(Gfx::IntPoint, i32, Vector<Web::HTML::SelectItem>, NonnullRefPtr<SelectDropdownReply> reply)
{
    reply->cancel();
}

void TabUserRequests::request_permission_prompt(PermissionPromptRequest, NonnullRefPtr<PermissionPromptReply> reply)
{
    reply->cancel();
}

void TabUserRequests::ensure_microphone_authorization(NonnullRefPtr<MicrophoneAuthorizationReply> reply)
{
    reply->cancel();
}

void TabUserRequests::request_geolocation_position(WebContentPage& page, u64 request_id, Web::HTML::CrossProcessId)
{
    page.async_geolocation_position_response(request_id, {}, to_underlying(Web::Geolocation::GeolocationPositionErrorCode::PositionUnavailable));
}

void TabUserRequests::start_geolocation_position_watch(WebContentPage& page, u64 request_id, Web::HTML::CrossProcessId)
{
    page.async_geolocation_position_response(request_id, {}, to_underlying(Web::Geolocation::GeolocationPositionErrorCode::PositionUnavailable));
}

void TabUserRequests::request_spell_check(Utf16String const&, NonnullRefPtr<SpellCheckReply> reply)
{
    reply->cancel();
}

void TabUserRequests::request_writing_suggestion(Utf16String const&, u32, NonnullRefPtr<WritingSuggestionReply> reply)
{
    reply->cancel();
}

void TabUserRequests::request_fullscreen_window(WebContentPage& page)
{
    page.async_deny_fullscreen_window();
}

}
