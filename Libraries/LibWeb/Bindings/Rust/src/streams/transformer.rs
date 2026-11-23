/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//! Transformer, https://streams.spec.whatwg.org/#transformer-api

use libjs_rust::api::Value;

use crate::bindings::callback::{CallbackFunction, PromiseCallbackFunction};

/// The dictionary Transformer of Streams/Transformer.idl, whose members the bindings convert.
pub(crate) struct Transformer {
    pub(crate) cancel: Option<PromiseCallbackFunction>,
    pub(crate) flush: Option<PromiseCallbackFunction>,
    pub(crate) readable_type: Option<Value>,
    pub(crate) start: Option<CallbackFunction>,
    pub(crate) transform: Option<PromiseCallbackFunction>,
    pub(crate) writable_type: Option<Value>,
}
