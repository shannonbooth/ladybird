/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/NonnullRefPtr.h>
#include <AK/Optional.h>
#include <AK/RefCounted.h>
#include <AK/Tuple.h>
#include <AK/Utf16String.h>
#include <AK/Vector.h>
#include <LibGfx/Color.h>
#include <LibWebCommon/HTML/SelectedFile.h>
#include <LibWebCommon/Page/SpellingError.h>
#include <LibWebCommon/Page/WritingSuggestion.h>
#include <LibWebCommon/Permissions/Permissions.h>

namespace WebView {

// A reply that is answered once. When whoever owes it goes away, cancel() gives the fallback answer instead.
class PendingReplyBase : public RefCounted<PendingReplyBase> {
public:
    virtual ~PendingReplyBase() = default;

    virtual bool is_pending() const = 0;
    virtual void cancel() = 0;

    // Takes down the UI that was meant to give the answer.
    Function<void()> on_cancel;
};

template<typename... Args>
class PendingReply final : public PendingReplyBase {
public:
    static NonnullRefPtr<PendingReply> create(Function<void(Args...)> reply, Args... fallback)
    {
        return adopt_ref(*new PendingReply(move(reply), Tuple<Args...> { move(fallback)... }));
    }

    virtual bool is_pending() const override { return static_cast<bool>(m_reply); }

    void complete(Args... args)
    {
        NonnullRefPtr protect = *this;
        auto reply = move(m_reply);
        if (!reply)
            return;
        on_cancel = nullptr;
        m_on_update = nullptr;
        reply(move(args)...);
    }

    virtual void cancel() override
    {
        NonnullRefPtr protect = *this;
        if (!m_reply)
            return;
        auto on_cancel = move(this->on_cancel);
        m_fallback.apply_as_args([this](auto&... args) { complete(move(args)...); });
        if (on_cancel)
            on_cancel();
    }

    // Answers that come before the reply, such as a colour picker's current colour.
    void set_on_update(Function<void(Args...)> on_update) { m_on_update = move(on_update); }

    void update(Args... args)
    {
        if (m_on_update)
            m_on_update(move(args)...);
    }

private:
    PendingReply(Function<void(Args...)> reply, Tuple<Args...> fallback)
        : m_reply(move(reply))
        , m_fallback(move(fallback))
    {
    }

    Function<void(Args...)> m_reply;
    Function<void(Args...)> m_on_update;
    Tuple<Args...> m_fallback;
};

using DialogReply = PendingReply<bool, Optional<Utf16String>>;
using ColorPickerReply = PendingReply<Optional<Color>>;
using DatePickerReply = PendingReply<Optional<Utf16String>>;
using FilePickerReply = PendingReply<Vector<Web::HTML::SelectedFile>>;
using SelectDropdownReply = PendingReply<Optional<u32>>;
using PermissionPromptReply = PendingReply<Optional<Web::Permissions::PromptOutcome>>;
using MicrophoneAuthorizationReply = PendingReply<bool>;
using SpellCheckReply = PendingReply<Vector<Web::SpellingError>>;
using WritingSuggestionReply = PendingReply<Optional<Web::WritingSuggestion>>;

}
