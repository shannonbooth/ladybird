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
    virtual void when_settled(Function<void()>) = 0;

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

    // Release admission/ownership before a completion callback can start another request.
    virtual void when_settled(Function<void()> callback) override
    {
        if (!is_pending()) {
            callback();
            return;
        }
        m_settlement_callbacks.append(move(callback));
    }

    void complete(Args... args)
    {
        NonnullRefPtr protect = *this;
        auto reply = move(m_reply);
        if (!reply)
            return;
        on_cancel = nullptr;
        m_on_update = nullptr;
        did_settle();
        reply(move(args)...);
    }

    virtual void cancel() override
    {
        NonnullRefPtr protect = *this;
        auto reply = move(m_reply);
        if (!reply)
            return;
        m_on_update = nullptr;
        // The reply is already inert while closing its widget. Do not admit a replacement until that finishes.
        auto on_cancel = move(this->on_cancel);
        if (on_cancel)
            on_cancel();
        did_settle();
        m_fallback.apply_as_args([&](auto&... args) { reply(move(args)...); });
    }

    // Answers that come before the reply, such as a colour picker's current colour.
    void set_on_update(Function<void(Args...)> on_update) { m_on_update = move(on_update); }

    void update(Args... args)
    {
        if (m_on_update)
            m_on_update(move(args)...);
    }

private:
    void did_settle()
    {
        for (auto& callback : exchange(m_settlement_callbacks, {}))
            callback();
    }

    PendingReply(Function<void(Args...)> reply, Tuple<Args...> fallback)
        : m_reply(move(reply))
        , m_fallback(move(fallback))
    {
    }

    Function<void(Args...)> m_reply;
    Function<void(Args...)> m_on_update;
    Tuple<Args...> m_fallback;
    Vector<Function<void()>> m_settlement_callbacks;
};

// The reply a callback of this type waits for.
template<typename>
struct PendingReplyForCallback;
template<typename... Args>
struct PendingReplyForCallback<Function<void(Args...)>> {
    using Type = PendingReply<Args...>;
};
template<typename Callback>
using ReplyFor = typename PendingReplyForCallback<Callback>::Type;

using DialogReply = PendingReply<bool, Optional<Utf16String>>;
using ColorPickerReply = PendingReply<Optional<Color>>;
using DatePickerReply = PendingReply<Optional<Utf16String>>;
using FilePickerReply = PendingReply<Vector<Web::HTML::SelectedFile>>;
using SelectDropdownReply = PendingReply<Optional<u32>>;
using PermissionPromptReply = PendingReply<Optional<Web::Permissions::PromptOutcome>>;
using MicrophoneAuthorizationReply = PendingReply<bool>;
using SpellCheckReply = PendingReply<Vector<Web::SpellingError>>;
using WritingSuggestionReply = PendingReply<Optional<Web::WritingSuggestion>>;
using ClosePreflightReply = PendingReply<bool>;

}
