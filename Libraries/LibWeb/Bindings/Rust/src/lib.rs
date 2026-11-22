/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//! The Rust code of LibWeb that runs on the LibJS runtime: the WebIDL bindings of the interfaces implemented in Rust,
//! and those implementations. The crate is the Rust part of LibWeb's library: it depends on the runtime and on
//! libweb_rust, and the library links its archive, so that the runtime is in a process once and this crate reaches it
//! as any Rust code reaches a crate it depends on.
//!
//! The crate's tests cover the code that does not touch the runtime. A test binary keeps every extern "C" function of
//! every crate it links, so linking the runtime would make it link every C function the runtime imports as well; the
//! modules that use the runtime are left out of the tests instead, as the runtime's own crate leaves out its engine.

// NB: The implementations of Serializable return concrete types where the trait has an opaque one. The trait is
//     pub(crate), so refining it exposes nothing.
#![allow(refining_impl_trait)]

#[cfg(feature = "allocator")]
extern crate ladybird_allocator;

#[path = "../../../../RustDemangle.rs"]
mod rust_demangle;

#[path = "../../../../RustPanic.rs"]
mod rust_panic;

pub(crate) mod ak_format;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod animations;
#[cfg(test)]
mod ffi_test_stubs;

#[cfg(not(test))]
#[allow(
    dead_code,
    reason = "the generated bindings use only the conversions their interfaces need"
)]
pub(crate) mod bindings;
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod canvas_2d;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod clipboard;
#[cfg(not(test))]
pub(crate) mod compositor_connection;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod compression;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod content_security_policy;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod cookie_store;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod credential_management;
#[cfg(not(test))]
pub(crate) mod crypto;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod css;
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
#[cfg_attr(
    test,
    allow(dead_code, reason = "the tests leave out the bindings, which use the interfaces")
)]
pub(crate) mod dom;
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
#[cfg_attr(
    test,
    allow(dead_code, reason = "the tests leave out the bindings, which use the interfaces")
)]
pub(crate) mod domurl;
pub(crate) mod encoding;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod encrypted_media_extensions;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod event_timing;
#[cfg(not(test))]
pub(crate) mod fetch;
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod file_api;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod gamepad;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod geolocation;
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
#[cfg_attr(
    test,
    allow(dead_code, reason = "the tests leave out the bindings, which use the interfaces")
)]
pub(crate) mod geometry;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod high_resolution_time;
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
#[cfg_attr(
    test,
    allow(dead_code, reason = "the tests leave out the bindings, which use the interfaces")
)]
pub(crate) mod html;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod indexed_db;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod internals;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod largest_contentful_paint;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod media_capture;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod media_session;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod media_source_extensions;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod navigation_timing;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod notifications_api;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod paint_timing;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod performance_timeline;
#[cfg(not(test))]
pub(crate) mod permissions;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod permissions_api;
#[cfg(not(test))]
pub(crate) mod permissions_policy;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod picture_in_picture;
#[cfg(not(test))]
pub(crate) mod platform;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod resource_timing;
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod server_timing;
pub(crate) mod speech;
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
#[cfg_attr(
    test,
    allow(dead_code, reason = "the tests leave out the bindings, which use the interfaces")
)]
pub(crate) mod storage_api;
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
#[cfg_attr(
    test,
    allow(dead_code, reason = "the tests leave out the bindings, which use the interfaces")
)]
pub(crate) mod streams;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod trusted_types;
#[cfg(not(test))]
pub(crate) mod ui_events;
pub(crate) mod url_pattern;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod user_timing;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod web_assembly;
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
#[cfg_attr(
    test,
    allow(dead_code, reason = "the tests leave out the bindings, which use the interfaces")
)]
pub(crate) mod web_gl;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every operation as one that can throw"
)]
pub(crate) mod web_locks;
#[cfg(not(test))]
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod web_vtt;
#[cfg(not(test))]
pub(crate) mod webidl;
#[cfg(not(test))]
pub(crate) mod webrtc;
#[allow(
    clippy::unnecessary_wraps,
    reason = "the bindings call every attribute and operation as one that can throw"
)]
pub(crate) mod xhr;

#[cfg(not(test))]
#[allow(clippy::unnecessary_wraps)]
pub(crate) mod web_audio;

// The generators of the build script, whose tests run with those of the crate.
#[cfg(test)]
#[path = "../generate/webgl_constants.rs"]
mod webgl_constants;
#[cfg(test)]
#[path = "../generate/webgl_proxy.rs"]
mod webgl_proxy;
#[cfg(test)]
#[allow(
    dead_code,
    reason = "the tests do not run the generators, which the model is for as well"
)]
#[path = "../../../../LibCompositing/Rust/generate/webgl_wire_model.rs"]
mod webgl_wire_model;

// NB: Little in this crate names libweb_rust, which C++ calls into. Linking it keeps its extern "C" functions in the
//     static library.
#[cfg(not(test))]
extern crate libweb_rust;
