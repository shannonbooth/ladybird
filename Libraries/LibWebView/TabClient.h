/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWebView/ViewImplementation.h>

namespace WebView {

// What the frontend showing a tab does for it: the dialogs, pickers and prompts its pages ask for, the changes they ask
// of the window it is in, and the tabs and windows opened from it. A frontend declines what it does not do.
class WEBVIEW_API TabClient {
public:
    virtual ~TabClient() = default;

    // Shows a tab or window a page of this tab opened, and gives its WebDriver handle. A frontend that does not leaves
    // the traversable without anything showing it.
    virtual String show_tab_opened_by_page(Web::HTML::ActivateTab, Web::HTML::WebViewHints, CanonicalTraversable&) { return {}; }
    virtual void activate_tab() { }

    virtual void request_alert(Utf16String const&, NonnullRefPtr<DialogReply> reply) { reply->cancel(); }
    virtual void request_confirm(Utf16String const&, NonnullRefPtr<DialogReply> reply) { reply->cancel(); }
    virtual void request_prompt(Utf16String const&, Utf16String const&, NonnullRefPtr<DialogReply> reply) { reply->cancel(); }
    virtual void request_before_unload(String const&, NonnullRefPtr<DialogReply> reply) { reply->cancel(); }
    virtual void request_set_prompt_text(Utf16String const&) { }
    virtual void request_accept_dialog() { }
    virtual void request_dismiss_dialog() { }

    virtual void request_color_picker(Color, NonnullRefPtr<ColorPickerReply> reply) { reply->cancel(); }
    virtual void request_date_picker(Utf16String const&, Utf16String const&, Utf16String const&, Utf16String const&, NonnullRefPtr<DatePickerReply> reply) { reply->cancel(); }
    virtual void request_file_picker(Web::HTML::FileFilter const&, Web::HTML::AllowMultipleFiles, NonnullRefPtr<FilePickerReply> reply) { reply->cancel(); }
    virtual void request_select_dropdown(Gfx::IntPoint, i32, Vector<Web::HTML::SelectItem>, NonnullRefPtr<SelectDropdownReply> reply) { reply->cancel(); }

    virtual void request_permission(PermissionPromptRequest const&, PermissionPromptCallback callback) { callback(Web::Permissions::PromptOutcome::Cancelled); }
    virtual void cancel_permission_prompt(PermissionPromptRequest const&) { }

    virtual bool can_confirm_external_url() const { return false; }
    virtual void request_external_url_confirmation(URL::URL const&, URL::Origin const&, ExternalURLHandler const&, Function<void(bool)> on_complete) { on_complete(false); }

    virtual bool can_look_up_in_dictionary() const { return false; }
    virtual void request_dictionary_lookup(DictionaryLookup const&, Gfx::IntPoint) { }

    virtual void request_website_issue_report() { }
    virtual void request_screenshot() { }

    virtual void restore_window() { }
    virtual void reposition_window(Gfx::IntPoint) { }
    virtual void resize_window(Gfx::IntSize) { }
    virtual void maximize_window() { }
    virtual void minimize_window() { }
    virtual bool can_fullscreen_window() const { return false; }
    virtual void fullscreen_window() { }
    virtual void exit_fullscreen_window() { }
};

}
