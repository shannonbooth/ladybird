/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Utf16String.h>
#include <AK/Vector.h>
#include <LibGC/Ptr.h>
#include <LibWeb/Bindings/MediaMetadata.h>
#include <LibWeb/Bindings/RustInterface.h>
#include <LibWeb/Forward.h>

namespace Web::MediaSession {

// https://w3c.github.io/mediasession/#dictdef-mediaimage
struct MediaImage {
    Utf16String src;
    Utf16String sizes;
    Utf16String type;
};

// https://w3c.github.io/mediasession/#mediametadata
class MediaMetadata final : public Bindings::RustInterfaceCell {
    WEB_RUST_INTERFACE(MediaMetadata, media_metadata)

public:
    Utf16String title() const;
    Utf16String artist() const;
    Utf16String album() const;
    Vector<MediaImage> artwork_images() const;
    u64 artwork_version() const;

    void set_media_session(GC::Ptr<MediaSession>);
};

}
