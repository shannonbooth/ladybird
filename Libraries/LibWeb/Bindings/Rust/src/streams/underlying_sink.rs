/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//! UnderlyingSink, https://streams.spec.whatwg.org/#underlying-sink-api

use libjs_rust::api::Value;

use crate::bindings::callback::{CallbackFunction, PromiseCallbackFunction};

/// The dictionary UnderlyingSink of Streams/UnderlyingSink.idl, whose members the bindings convert.
pub(crate) struct UnderlyingSink {
    pub(crate) abort: Option<PromiseCallbackFunction>,
    pub(crate) close: Option<PromiseCallbackFunction>,
    pub(crate) start: Option<CallbackFunction>,
    pub(crate) r#type: Option<Value>,
    pub(crate) write: Option<PromiseCallbackFunction>,
}
