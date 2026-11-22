/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//! MediaMetadata, https://w3c.github.io/mediasession/#mediametadata

use core::cell::RefCell;
use core::ffi::c_void;

use ak::Utf16String;
use libjs_rust::api::{Gc, GcRefCell, IntegrityLevel, JSVM, MarkedVec, Must, Object, Trace, Value, Vm, vm_from_abi};
use liburl_rust::url::ExcludeFragment;
use liburl_rust::{BasicParseOptions, basic_parse};

use super::chapter_information::{ChapterInformation, ChapterInformationInit};
use crate::bindings::conversions::{
    FromJs, ToJs, UsvString, frozen_array_to_js, rooted_sequence_to_js, sequence_to_js,
};
use crate::bindings::cpp_cell::CppCell;
use crate::bindings::environment_settings_object::current_settings_object;
use crate::bindings::interface::{Ref, Wrappable, allocate, cell_from_abi, wrap};
use crate::bindings::setlike::WorldEntries;
use crate::bindings::{CallContext, Exception, WebIdlResult};
use crate::domurl::url_input;

unsafe extern "C" {
    fn web_media_session_state_changed(media_session: *mut c_void);
}

/// The dictionary MediaImage of MediaSession/MediaMetadata.idl.
#[derive(Clone)]
pub(crate) struct MediaImage {
    pub(crate) src: UsvString,
    pub(crate) sizes: Utf16String,
    pub(crate) r#type: Utf16String,
}

/// The dictionary MediaMetadataInit of MediaSession/MediaMetadata.idl.
pub(crate) struct MediaMetadataInit {
    pub(crate) title: Utf16String,
    pub(crate) artist: Utf16String,
    pub(crate) album: Utf16String,
    pub(crate) artwork: Vec<MediaImage>,
    pub(crate) chapter_info: Vec<ChapterInformationInit>,
}

// https://w3c.github.io/mediasession/#convert-artwork-algorithm
pub(crate) fn convert_artwork(vm: &Vm, input: Vec<MediaImage>) -> WebIdlResult<Vec<MediaImage>> {
    // 1. Let output be an empty list of type MediaImage.
    let mut output = Vec::with_capacity(input.len());

    // 2. For each entry in input (which is a MediaImage list), perform the following steps:
    for entry in input {
        // 1. Let image be a new MediaImage.
        // NB: The image is created once its members are known.

        // 2. Let baseURL be the API base URL specified by the entry settings object.
        // AD-HOC: Use the current settings object's base URL instead of the entry settings object's, as the
        //         web-platform-tests expect. Artwork built with another realm's constructor uses that realm's base URL.
        let base_url = current_settings_object(vm).api_base_url();

        // 3. Parse entry's src using baseURL. If it does not return failure, set image's src to the return value.
        //    Otherwise, throw a TypeError and abort these steps.
        let Some(url) = basic_parse(
            url_input(entry.src.units()),
            BasicParseOptions::new().base_url(&base_url),
        ) else {
            return Err(Exception::TypeError(Utf16String::from_utf8(
                "Artwork src could not be parsed as a URL",
            )));
        };
        let src = UsvString::from_utf8(url.serialize(ExcludeFragment::No));

        // 4. Set image's sizes to entry's sizes.
        // 5. Set image's type to entry's type.
        // 6. Append image to the output.
        output.push(MediaImage {
            src,
            sizes: entry.sizes,
            r#type: entry.r#type,
        });
    }

    // 3. Return output as result.
    Ok(output)
}

struct MediaMetadataState {
    title: Utf16String,
    artist: Utf16String,
    album: Utf16String,
    artwork_images: Vec<MediaImage>,
    artwork_version: u64,
}

// https://w3c.github.io/mediasession/#mediametadata
#[derive(Trace)]
#[gc(crate = libjs_rust)]
pub(crate) struct MediaMetadata {
    #[gc(untraced)]
    state: RefCell<MediaMetadataState>,

    // The MediaMetadata's converted artwork images, created once for each wrapper world that gets them, and dropped
    // whenever the artwork is set.
    converted_artwork_images: WorldEntries<Object>,
    // The entries hold their arrays weakly, so these keep them alive: Otherwise a script that keeps just an element of
    // one would get a new array, with new elements, after a collection.
    retained_converted_artwork_images: GcRefCell<Vec<Gc<Object>>>,

    // https://w3c.github.io/mediasession/#mediametadata-chapter-information
    chapter_information: Vec<Ref<ChapterInformation>>,

    media_session: GcRefCell<Option<CppCell>>,
}

impl Wrappable for MediaMetadata {}

impl MediaMetadata {
    // https://w3c.github.io/mediasession/#dom-mediametadata-mediametadata
    pub(crate) fn construct(cx: &mut CallContext, init: MediaMetadataInit) -> WebIdlResult<Ref<MediaMetadata>> {
        // 1. Let metadata be a new MediaMetadata object.
        // NB: The object is allocated once its members are known, so that its chapter information never changes.

        // 2. Set metadata's title to init's title.
        // 3. Set metadata's artist to init's artist.
        // 4. Set metadata's album to init's album.

        // 5. Run the convert artwork algorithm with init's artwork as input and set metadata's artwork images as the
        //    result if it succeeded.
        let artwork_images = convert_artwork(cx.vm(), init.artwork)?;

        // 6. Let chapters be an empty list of type ChapterInformation.
        // 7. For each entry in init's chapterInfo, create a ChapterInformation from entry and append it to chapters.
        // 8. Set metadata's chapter information to the result of creating a frozen array from chapters.
        // NB: The chapters go straight into metadata's chapter information, which keeps them alive. The frozen array is
        //     created when the chapterInfo attribute is first read in a wrapper world.
        let chapters = MarkedVec::with_capacity(cx.vm(), init.chapter_info.len());
        for entry in init.chapter_info {
            chapters.push(ChapterInformation::create(cx.vm(), entry)?);
        }

        // 9. Return metadata.
        Ok(allocate(cx.vm(), |_| MediaMetadata {
            state: RefCell::new(MediaMetadataState {
                title: init.title,
                artist: init.artist,
                album: init.album,
                artwork_images,
                artwork_version: 0,
            }),
            converted_artwork_images: WorldEntries::default(),
            retained_converted_artwork_images: GcRefCell::new(Vec::new()),
            chapter_information: chapters.to_vec(),
            media_session: GcRefCell::new(None),
        }))
    }

    pub(crate) fn title(&self, _: &mut CallContext) -> WebIdlResult<Utf16String> {
        Ok(self.state.borrow().title.clone())
    }

    // https://w3c.github.io/mediasession/#dom-mediametadata-title
    pub(crate) fn set_title(&self, _: &mut CallContext, value: Utf16String) -> WebIdlResult<()> {
        // On setting, it MUST set the MediaMetadata's title to the given value.
        self.state.borrow_mut().title = value;
        self.metadata_updated();
        Ok(())
    }

    pub(crate) fn artist(&self, _: &mut CallContext) -> WebIdlResult<Utf16String> {
        Ok(self.state.borrow().artist.clone())
    }

    // https://w3c.github.io/mediasession/#dom-mediametadata-artist
    pub(crate) fn set_artist(&self, _: &mut CallContext, value: Utf16String) -> WebIdlResult<()> {
        // On setting, it MUST set the MediaMetadata's artist to the given value.
        self.state.borrow_mut().artist = value;
        self.metadata_updated();
        Ok(())
    }

    pub(crate) fn album(&self, _: &mut CallContext) -> WebIdlResult<Utf16String> {
        Ok(self.state.borrow().album.clone())
    }

    // https://w3c.github.io/mediasession/#dom-mediametadata-album
    pub(crate) fn set_album(&self, _: &mut CallContext, value: Utf16String) -> WebIdlResult<()> {
        // On setting, it MUST set the MediaMetadata's album to the given value.
        self.state.borrow_mut().album = value;
        self.metadata_updated();
        Ok(())
    }

    // https://w3c.github.io/mediasession/#dom-mediametadata-artwork
    pub(crate) fn artwork(&self, cx: &mut CallContext) -> WebIdlResult<Value> {
        // 1. If the MediaMetadata's converted artwork images is undefined, run the following steps:
        // NB: We keep the converted artwork images for each wrapper world that reads them, so that no world gets
        //     another's array.
        let converted_artwork_images = self.converted_artwork_images.get_or_create(cx, || {
            // 1. Let frozenArtwork be a JavaScript Array value.
            // 2. For each entry in the MediaMetadata's artwork images, perform the following steps:
            let artwork_images = self.state.borrow().artwork_images.clone();
            let frozen_artwork = sequence_to_js(cx, artwork_images, |entry| {
                // 1. Let image be the result of converting to a JavaScript object entry.
                let image = entry.to_js(cx)?;

                // 2. Perform SetIntegrityLevel(image, "frozen"), to prevent accidental mutation by scripts.
                image
                    .as_object()
                    .set_integrity_level(cx.vm(), IntegrityLevel::Frozen)
                    .must();

                // 3. Push image to frozenArtwork.
                Ok(image)
            });

            // 3. Perform SetIntegrityLevel(frozenArtwork, "frozen").
            let frozen_artwork = frozen_array_to_js(cx, frozen_artwork).must().as_object();

            // 4. Set the MediaMetadata's converted artwork images to frozenArtwork.
            self.retained_converted_artwork_images
                .borrow_mut(cx.vm())
                .push(frozen_artwork);
            frozen_artwork
        });

        // 2. Return the MediaMetadata's converted artwork images.
        Ok(Value::from_object(converted_artwork_images))
    }

    // https://w3c.github.io/mediasession/#dom-mediametadata-artwork
    pub(crate) fn set_artwork(&self, cx: &mut CallContext, value: MarkedVec<'_, Gc<Object>>) -> WebIdlResult<()> {
        // 1. Let convertedArtwork be the result of converting value to a sequence of type MediaImage.
        let mut converted_artwork = Vec::with_capacity(value.len());
        for entry in value.values().iter() {
            converted_artwork.push(MediaImage::from_js(cx.vm(), Value::from_object(*entry))?);
        }

        // 2. Run convert artwork algorithm with convertedArtwork, and set the MediaMetadata's artwork images as the
        //    result if it succeeds.
        let artwork_images = convert_artwork(cx.vm(), converted_artwork)?;
        {
            let mut state = self.state.borrow_mut();
            state.artwork_images = artwork_images;
            state.artwork_version += 1;
        }

        // 3. Set the MediaMetadata's converted artwork images to undefined.
        self.converted_artwork_images.clear();
        self.retained_converted_artwork_images.borrow_mut(cx.vm()).clear();
        self.metadata_updated();
        Ok(())
    }

    // https://w3c.github.io/mediasession/#dom-mediametadata-chapterinfo
    pub(crate) fn chapter_info(&self, cx: &mut CallContext) -> WebIdlResult<Value> {
        // On getting, it MUST return the MediaMetadata's chapter information.
        let chapters = MarkedVec::with_capacity(cx.vm(), self.chapter_information.len());
        chapters.extend_from_slice(&self.chapter_information);
        let array = rooted_sequence_to_js(cx, chapters, |chapter| {
            let wrapper = wrap(cx, *chapter);

            // AD-HOC: The spec freezes only the array, but each ChapterInformation object is frozen too, as the
            //         web-platform-tests expect.
            wrapper.set_integrity_level(cx.vm(), IntegrityLevel::Frozen).must();
            Ok(Value::from_object(wrapper))
        });
        Ok(frozen_array_to_js(cx, array).must())
    }

    // https://w3c.github.io/mediasession/#the-mediametadata-interface
    fn metadata_updated(&self) {
        // 1. If the instance has no associated media session, abort these steps.
        let Some(media_session) = self.media_session.borrow().clone() else {
            return;
        };

        // 2. Otherwise, queue a task to run the following substeps:
        //    1. If the instance no longer has an associated media session, abort these steps.
        //    2. Otherwise, in parallel, run the update metadata algorithm.
        // NB: The deferred update reads the active session's metadata, so metadata detached before it runs isn't
        //     reported.
        // SAFETY: The MediaSession is live.
        unsafe { web_media_session_state_changed(media_session.as_ptr()) };
    }
}

/// # Safety
///
/// `metadata` must be a live MediaMetadata.
unsafe fn metadata_from_abi(metadata: *mut c_void) -> Ref<MediaMetadata> {
    // SAFETY: The caller passes a live MediaMetadata.
    unsafe { cell_from_abi::<MediaMetadata>(metadata) }
}

/// The title of the MediaMetadata, whose reference the caller takes.
///
/// # Safety
///
/// `metadata` must be a live MediaMetadata.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn web_rust_media_metadata_title(metadata: *mut c_void) -> usize {
    // SAFETY: The caller passes a live MediaMetadata.
    let metadata = unsafe { metadata_from_abi(metadata) };
    metadata.state.borrow().title.clone().into_raw()
}

/// The artist of the MediaMetadata, whose reference the caller takes.
///
/// # Safety
///
/// `metadata` must be a live MediaMetadata.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn web_rust_media_metadata_artist(metadata: *mut c_void) -> usize {
    // SAFETY: The caller passes a live MediaMetadata.
    let metadata = unsafe { metadata_from_abi(metadata) };
    metadata.state.borrow().artist.clone().into_raw()
}

/// The album of the MediaMetadata, whose reference the caller takes.
///
/// # Safety
///
/// `metadata` must be a live MediaMetadata.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn web_rust_media_metadata_album(metadata: *mut c_void) -> usize {
    // SAFETY: The caller passes a live MediaMetadata.
    let metadata = unsafe { metadata_from_abi(metadata) };
    metadata.state.borrow().album.clone().into_raw()
}

/// How many times the artwork of the MediaMetadata was set.
///
/// # Safety
///
/// `metadata` must be a live MediaMetadata.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn web_rust_media_metadata_artwork_version(metadata: *mut c_void) -> u64 {
    // SAFETY: The caller passes a live MediaMetadata.
    let metadata = unsafe { metadata_from_abi(metadata) };
    metadata.state.borrow().artwork_version
}

/// Calls `on_image` with the src, sizes and type of each artwork image of the MediaMetadata, which are lent for the
/// call.
///
/// # Safety
///
/// `metadata` must be a live MediaMetadata.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn web_rust_media_metadata_for_each_artwork_image(
    metadata: *mut c_void,
    ctx: *mut c_void,
    on_image: unsafe extern "C" fn(*mut c_void, usize, usize, usize),
) {
    // SAFETY: The caller passes a live MediaMetadata.
    let metadata = unsafe { metadata_from_abi(metadata) };
    let artwork_images = metadata.state.borrow().artwork_images.clone();
    for image in &artwork_images {
        // SAFETY: The strings outlive the call.
        unsafe {
            on_image(
                ctx,
                image.src.as_utf16_string().raw_identity(),
                image.sizes.raw_identity(),
                image.r#type.raw_identity(),
            );
        };
    }
}

/// Sets the media session of the MediaMetadata, which is null or a live MediaSession.
///
/// # Safety
///
/// `vm` must be the live VM, `metadata` a live MediaMetadata, and `media_session` null or a live MediaSession.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn web_rust_media_metadata_set_media_session(
    vm: *mut JSVM,
    metadata: *mut c_void,
    media_session: *mut c_void,
) {
    // SAFETY: The caller passes the live VM and a live MediaMetadata.
    let (vm, metadata) = unsafe { (vm_from_abi(vm), metadata_from_abi(metadata)) };
    // SAFETY: The caller passes null or a live MediaSession.
    let media_session = (!media_session.is_null()).then(|| unsafe { CppCell::from_raw(media_session) });
    *metadata.media_session.borrow_mut(vm) = media_session;
}
