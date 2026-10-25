/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//! The libjs_rust items the lints know by path, at the same paths, and the types they find through its marker traits.

pub mod layout {
    pub mod cell {
        pub struct Gc<T: ?Sized>(core::ptr::NonNull<T>);

        impl<T: ?Sized> Clone for Gc<T> {
            fn clone(&self) -> Self {
                *self
            }
        }

        impl<T: ?Sized> Copy for Gc<T> {}

        impl<T: crate::gc::class::GcCell> core::ops::Deref for Gc<T> {
            type Target = T;

            fn deref(&self) -> &T {
                // SAFETY: Stand-in.
                unsafe { self.0.as_ref() }
            }
        }
    }

    pub mod value {
        #[derive(Clone, Copy)]
        pub struct Value(pub u64);
    }
}

pub mod gc {
    pub mod visitor {
        pub struct Visitor;

        /// # Safety
        ///
        /// Stand-in.
        pub unsafe trait Trace {
            fn trace(&self, visitor: &mut Visitor);
        }

        unsafe impl<T> Trace for crate::layout::cell::Gc<T> {
            fn trace(&self, _: &mut Visitor) {}
        }

        unsafe impl<T: Trace> Trace for Vec<T> {
            fn trace(&self, _: &mut Visitor) {}
        }
    }

    pub mod class {
        /// # Safety
        ///
        /// Stand-in.
        pub unsafe trait GcCell: super::visitor::Trace {}
    }

    pub mod root {
        use super::visitor::Trace;

        pub struct Vm;

        pub struct Root<'vm, T: Trace + 'static> {
            pub vm: &'vm Vm,
            pub value: Box<T>,
        }

        pub struct MarkedVec<'vm, T: Trace + Clone + 'static> {
            pub vm: &'vm Vm,
            pub values: core::cell::OnceCell<Root<'vm, Vec<T>>>,
        }

        impl<'vm, T: Trace + Clone + 'static> MarkedVec<'vm, T> {
            pub fn new(vm: &'vm Vm) -> Self {
                Self {
                    vm,
                    values: core::cell::OnceCell::new(),
                }
            }
        }
    }

    pub mod capi {
        unsafe extern "C" {
            pub fn gc_heap_allocate_cell();
            pub fn gc_heap_defer_gc();
        }
    }

    pub mod gc_ref_cell {
        pub struct GcRefCell<T>(pub core::cell::RefCell<T>);

        impl<T> GcRefCell<T> {
            pub fn borrow_mut(&self) -> GcRefMut<'_, T> {
                GcRefMut(self.0.borrow_mut())
            }
        }

        pub struct GcRefMut<'a, T>(pub core::cell::RefMut<'a, T>);

        impl<T> core::ops::Deref for GcRefMut<'_, T> {
            type Target = T;

            fn deref(&self) -> &T {
                &self.0
            }
        }

        impl<T> core::ops::DerefMut for GcRefMut<'_, T> {
            fn deref_mut(&mut self) -> &mut T {
                &mut self.0
            }
        }
    }

    pub mod defer_gc {
        pub struct DeferGc<'vm>(pub &'vm super::root::Vm);

        impl Drop for DeferGc<'_> {
            fn drop(&mut self) {}
        }

        impl DeferGc<'_> {
            pub fn cover<T>(&self, value: T) -> Deferred<'_, T> {
                Deferred(value, core::marker::PhantomData)
            }
        }

        pub struct Deferred<'d, T>(pub T, pub core::marker::PhantomData<&'d ()>);
    }

    pub mod lint_markers {
        pub trait GcPointer {}

        impl<T: ?Sized> GcPointer for crate::layout::cell::Gc<T> {}
        impl GcPointer for crate::layout::value::Value {}

        /// # Safety
        ///
        /// Stand-in.
        pub unsafe trait KeepsAlive {}

        unsafe impl<T: super::visitor::Trace + 'static> KeepsAlive for super::root::Root<'_, T> {}
        unsafe impl<T: super::visitor::Trace + Clone + 'static> KeepsAlive for super::root::MarkedVec<'_, T> {}
        unsafe impl<T> KeepsAlive for super::defer_gc::Deferred<'_, T> {}

        /// # Safety
        ///
        /// Stand-in.
        pub unsafe trait DefersCollections {}

        unsafe impl DefersCollections for super::defer_gc::DeferGc<'_> {}

        pub trait ForbidsCollections {}

        impl<T> ForbidsCollections for super::gc_ref_cell::GcRefMut<'_, T> {}
        unsafe impl<T> DefersCollections for super::defer_gc::Deferred<'_, T> {}
    }

    pub mod weak {
        /// # Safety
        ///
        /// Stand-in.
        pub unsafe trait SweptAfterCollection {}

        unsafe impl<T> SweptAfterCollection for GcWeak<T> {}

        pub struct GcWeak<T> {
            pub slot: usize,
            pub cell: core::marker::PhantomData<crate::layout::cell::Gc<T>>,
        }
    }
}

pub use gc::defer_gc::{DeferGc, Deferred};
pub use gc::gc_ref_cell::{GcRefCell, GcRefMut};
pub use gc::root::{MarkedVec, Root, Vm};
pub use gc::visitor::{Trace, Visitor};
pub use gc::weak::GcWeak;
pub use layout::cell::Gc;
pub use layout::value::Value;

pub struct Cell;

unsafe impl Trace for Cell {
    fn trace(&self, _: &mut Visitor) {}
}

unsafe impl gc::class::GcCell for Cell {}

impl Cell {
    pub fn method(&self) {}
}
