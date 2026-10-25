/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use core::cell::{Ref, RefCell, RefMut};
use core::ops::{Deref, DerefMut};

use super::visitor::{Trace, Visitor};

/// Interior mutability for the compound state of a cell. A collection can happen at any allocation and needs to read
/// the state, so nothing may allocate while holding a mutable borrow; a collection that finds one stops the process.
pub struct GcRefCell<T>(RefCell<T>);

impl<T> GcRefCell<T> {
    pub const fn new(value: T) -> Self {
        Self(RefCell::new(value))
    }

    pub fn borrow(&self) -> Ref<'_, T> {
        self.0.borrow()
    }

    pub fn borrow_mut(&self) -> GcRefMut<'_, T> {
        GcRefMut(self.0.borrow_mut())
    }

    pub fn replace(&self, value: T) -> T {
        self.0.replace(value)
    }
}

impl<T: Default> Default for GcRefCell<T> {
    fn default() -> Self {
        Self::new(T::default())
    }
}

// SAFETY: Traces the current value.
unsafe impl<T: Trace> Trace for GcRefCell<T> {
    fn trace(&self, visitor: &mut Visitor) {
        let Ok(value) = self.0.try_borrow() else {
            eprintln!("libjs_rust: garbage collection while a GcRefCell is mutably borrowed");
            std::process::abort();
        };
        value.trace(visitor);
    }
}

/// A mutable borrow of the state in a GcRefCell, while which nothing may collect garbage.
pub struct GcRefMut<'a, T: ?Sized>(RefMut<'a, T>);

impl<'a, T: ?Sized> GcRefMut<'a, T> {
    /// Borrows a part of the state, as RefMut::map() does.
    pub fn map<U: ?Sized>(borrow: Self, part: impl FnOnce(&mut T) -> &mut U) -> GcRefMut<'a, U> {
        GcRefMut(RefMut::map(borrow.0, part))
    }
}

impl<T: ?Sized> Deref for GcRefMut<'_, T> {
    type Target = T;

    fn deref(&self) -> &T {
        &self.0
    }
}

impl<T: ?Sized> DerefMut for GcRefMut<'_, T> {
    fn deref_mut(&mut self) -> &mut T {
        &mut self.0
    }
}
