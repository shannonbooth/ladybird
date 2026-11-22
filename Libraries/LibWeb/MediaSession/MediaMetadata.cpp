/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/HostClassBuilder.h>
#include <LibJS/Runtime/VM.h>
#include <LibWeb/MediaSession/MediaMetadata.h>
#include <LibWeb/MediaSession/MediaSession.h>

namespace Web::MediaSession {

extern "C" {
// Defined in Bindings/Rust/src/media_session/media_metadata.rs.
FlatPtr web_rust_media_metadata_title(MediaMetadata const*);
FlatPtr web_rust_media_metadata_artist(MediaMetadata const*);
FlatPtr web_rust_media_metadata_album(MediaMetadata const*);
u64 web_rust_media_metadata_artwork_version(MediaMetadata const*);
void web_rust_media_metadata_for_each_artwork_image(MediaMetadata const*, void* ctx, void (*on_image)(void*, FlatPtr src, FlatPtr sizes, FlatPtr type));
void web_rust_media_metadata_set_media_session(JSVM*, MediaMetadata*, MediaSession*);

// Called by Bindings/Rust/src/media_session/media_metadata.rs.
void web_media_session_state_changed(MediaSession*);
}

Utf16String MediaMetadata::title() const
{
    return Utf16String::adopt_raw(web_rust_media_metadata_title(this));
}

Utf16String MediaMetadata::artist() const
{
    return Utf16String::adopt_raw(web_rust_media_metadata_artist(this));
}

Utf16String MediaMetadata::album() const
{
    return Utf16String::adopt_raw(web_rust_media_metadata_album(this));
}

Vector<MediaImage> MediaMetadata::artwork_images() const
{
    Vector<MediaImage> images;
    web_rust_media_metadata_for_each_artwork_image(this, &images, [](void* ctx, FlatPtr src, FlatPtr sizes, FlatPtr type) {
        static_cast<Vector<MediaImage>*>(ctx)->append({ Utf16String::from_raw(src), Utf16String::from_raw(sizes), Utf16String::from_raw(type) });
    });
    return images;
}

u64 MediaMetadata::artwork_version() const
{
    return web_rust_media_metadata_artwork_version(this);
}

void MediaMetadata::set_media_session(GC::Ptr<MediaSession> media_session)
{
    web_rust_media_metadata_set_media_session(JS::HostABI::vm_to_abi(JS::VM::the()), this, media_session.ptr());
}

void web_media_session_state_changed(MediaSession* media_session)
{
    media_session->state_changed();
}

}
