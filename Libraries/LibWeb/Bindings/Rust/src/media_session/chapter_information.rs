/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//! ChapterInformation, https://w3c.github.io/mediasession/#chapterinformation

use ak::Utf16String;
use libjs_rust::api::{Trace, Vm};

use super::media_metadata::{MediaImage, convert_artwork};
use crate::bindings::interface::{Ref, Wrappable, allocate};
use crate::bindings::{CallContext, Exception, WebIdlResult};

/// The dictionary ChapterInformationInit of MediaSession/ChapterInformation.idl.
pub(crate) struct ChapterInformationInit {
    pub(crate) title: Utf16String,
    pub(crate) start_time: f64,
    pub(crate) artwork: Vec<MediaImage>,
}

// https://w3c.github.io/mediasession/#chapterinformation
#[derive(Trace)]
#[gc(crate = libjs_rust)]
pub(crate) struct ChapterInformation {
    #[gc(untraced)]
    title: Utf16String,
    start_time: f64,
    #[gc(untraced)]
    artwork_images: Vec<MediaImage>,
}

impl Wrappable for ChapterInformation {}

impl ChapterInformation {
    // https://w3c.github.io/mediasession/#create-a-chapterinformation
    pub(crate) fn create(vm: &Vm, init: ChapterInformationInit) -> WebIdlResult<Ref<ChapterInformation>> {
        // 1. Let chapterInfo be a new ChapterInformation object.
        // NB: The object is allocated once its members are known, so that it never changes after it is created.

        // 2. Set chapterInfo's title to init's title.
        let title = init.title;

        // 3. Set chapterInfo's startTime to init's startTime. If the startTime is negative or greater than duration,
        //    throw a TypeError.
        // NB: Nothing here has a duration, so only a negative startTime is rejected.
        if init.start_time < 0.0 {
            return Err(Exception::TypeError(Utf16String::from_utf8(
                "Chapter startTime must not be negative",
            )));
        }
        let start_time = init.start_time;

        // 4. Let artwork be the result of running the convert artwork algorithm with init's artwork as input.
        // 5. Set chapterInfo's artwork images to the result of creating a frozen array from artwork.
        // NB: The bindings create the frozen array when the artwork attribute is first read in a wrapper world.
        let artwork_images = convert_artwork(vm, init.artwork)?;

        // 6. Return chapterInfo.
        Ok(allocate(vm, |_| ChapterInformation {
            title,
            start_time,
            artwork_images,
        }))
    }

    pub(crate) fn title(&self, _: &mut CallContext) -> WebIdlResult<Utf16String> {
        Ok(self.title.clone())
    }

    pub(crate) fn start_time(&self, _: &mut CallContext) -> WebIdlResult<f64> {
        Ok(self.start_time)
    }

    // https://w3c.github.io/mediasession/#dom-chapterinformation-artwork
    pub(crate) fn artwork(&self, _: &mut CallContext) -> WebIdlResult<Vec<MediaImage>> {
        // On getting, it MUST return the ChapterInformation's artwork images.
        Ok(self.artwork_images.clone())
    }
}
