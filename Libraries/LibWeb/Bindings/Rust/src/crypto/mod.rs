/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//! The Web Cryptography API.

#[allow(clippy::module_inception, reason = "the module mirrors the Crypto interface file")]
pub(crate) mod crypto;
pub(crate) mod crypto_algorithms;
pub(crate) mod crypto_bindings;
pub(crate) mod crypto_key;
pub(crate) mod subtle_crypto;
