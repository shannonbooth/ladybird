/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//! Dispatch to the steps of serializable interfaces. The generated Rust bindings classify their wrappers exactly,
//! and C++ reports the primary interface of objects that it implements.

use libjs_rust::api::{Gc, Object};

use crate::html::structured_serialize::cpp::cpp_serializable_interface_name;

/// The identifier stored for the primary interface of a serializable object.
pub(crate) struct SerializableRegistryEntry {
    pub(crate) interface_name: u16,
    pub(crate) identifier: &'static [u8],
}

/// Whose Rust steps serialize and deserialize an interface's objects.
pub(crate) enum SerializableSteps {
    Rust,
    DOMException,
    QuotaExceededError,
    RTCEncodedAudioFrame,
}

// The version after every serializable object's interface identifier in a storage record. Changing it requires
// migration.
pub(crate) const SERIALIZABLE_PAYLOAD_VERSION: u64 = 1;

use crate::bindings::generated::serializable_objects as generated;

pub(crate) use generated::{
    STORAGE_INTERFACES, deserialization_steps, is_ipc_interface, serializable_interface_name_from_storage_identifier,
    serializable_interface_storage_identifier, serializable_steps, serialization_steps,
};

pub(crate) fn serializable_interface_name(object: Gc<Object>) -> Option<u16> {
    generated::rust_interface_name(object).or_else(|| {
        cpp_serializable_interface_name(object).filter(|&name| generated::is_cpp_serializable_interface(name))
    })
}
