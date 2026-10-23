/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//! Headers, https://fetch.spec.whatwg.org/#headers-class

use core::cell::Cell;
use core::ffi::c_void;
use core::ptr::NonNull;

use ak::{Span, Utf16String};
use libjs_rust::api::Trace;

use crate::bindings::conversions::ByteString;
use crate::bindings::interface::Wrappable;
use crate::bindings::{CallContext, Exception, WebIdlResult};
use crate::fetch::infrastructure::http::headers::{
    is_forbidden_request_header, is_forbidden_response_header_name, is_header_name, is_header_value,
    is_no_cors_safelisted_request_header, is_no_cors_safelisted_request_header_name,
    is_privileged_no_cors_request_header_name, normalize,
};

type OnBytes = unsafe extern "C" fn(ctx: *mut c_void, bytes: Span<'_, u8>);
type OnHeader = unsafe extern "C" fn(ctx: *mut c_void, name: Span<'_, u8>, value: Span<'_, u8>);

// NB: A header list is shared with the request or response that fetching works on, so it stays in C++
//     (Libraries/LibWeb/Fetch/Headers.cpp).
unsafe extern "C" {
    /// A new empty HTTP::HeaderList, one reference to which the caller owns.
    fn web_fetch_header_list_create() -> *mut c_void;
    fn web_fetch_header_list_unref(list: *mut c_void);
    fn web_fetch_header_list_contains(list: *mut c_void, name: Span<'_, u8>) -> bool;
    fn web_fetch_header_list_get(list: *mut c_void, name: Span<'_, u8>, ctx: *mut c_void, on_value: OnBytes) -> bool;
    fn web_fetch_header_list_delete(list: *mut c_void, name: Span<'_, u8>);
    fn web_fetch_header_list_set(list: *mut c_void, name: Span<'_, u8>, value: Span<'_, u8>);
    fn web_fetch_header_list_append(list: *mut c_void, name: Span<'_, u8>, value: Span<'_, u8>);
    fn web_fetch_header_list_for_each(list: *mut c_void, ctx: *mut c_void, on_header: OnHeader);
    fn web_fetch_header_list_sort_and_combine(list: *mut c_void, ctx: *mut c_void, on_header: OnHeader);
}

/// A reference to a C++ HTTP::HeaderList, which is reference counted.
pub(crate) struct HeaderList(NonNull<c_void>);

impl HeaderList {
    /// # Safety
    ///
    /// `list` must be a live HTTP::HeaderList, one reference to which the new value adopts.
    pub(crate) unsafe fn adopt(list: *mut c_void) -> Self {
        Self(NonNull::new(list).expect("C++ passes a header list"))
    }

    pub(crate) fn as_ptr(&self) -> *mut c_void {
        self.0.as_ptr()
    }

    fn contains(&self, name: &[u8]) -> bool {
        // SAFETY: The list is live, and the name outlives the call.
        unsafe { web_fetch_header_list_contains(self.as_ptr(), Span::new(name)) }
    }

    fn get(&self, name: &[u8]) -> Option<Vec<u8>> {
        unsafe extern "C" fn on_value(ctx: *mut c_void, value: Span<'_, u8>) {
            // SAFETY: ctx is the value below, which outlives the call.
            unsafe { *ctx.cast::<Vec<u8>>() = value.as_slice().to_vec() };
        }

        let mut value = Vec::new();
        // SAFETY: The list is live, and the name and the value outlive the call.
        unsafe { web_fetch_header_list_get(self.as_ptr(), Span::new(name), (&raw mut value).cast(), on_value) }
            .then_some(value)
    }

    fn delete(&self, name: &[u8]) {
        // SAFETY: The list is live, and the name outlives the call.
        unsafe { web_fetch_header_list_delete(self.as_ptr(), Span::new(name)) };
    }

    fn set(&self, name: &[u8], value: &[u8]) {
        // SAFETY: The list is live, and the name and the value outlive the call.
        unsafe { web_fetch_header_list_set(self.as_ptr(), Span::new(name), Span::new(value)) };
    }

    fn append(&self, name: &[u8], value: &[u8]) {
        // SAFETY: The list is live, and the name and the value outlive the call.
        unsafe { web_fetch_header_list_append(self.as_ptr(), Span::new(name), Span::new(value)) };
    }

    fn collect_headers(
        &self,
        walk: unsafe extern "C" fn(list: *mut c_void, ctx: *mut c_void, on_header: OnHeader),
    ) -> Vec<(Vec<u8>, Vec<u8>)> {
        unsafe extern "C" fn on_header(ctx: *mut c_void, name: Span<'_, u8>, value: Span<'_, u8>) {
            // SAFETY: ctx is the headers below, which outlive the call.
            let headers = unsafe { &mut *ctx.cast::<Vec<(Vec<u8>, Vec<u8>)>>() };
            headers.push((name.as_slice().to_vec(), value.as_slice().to_vec()));
        }

        let mut headers = Vec::new();
        // SAFETY: The list is live, and the headers outlive the call.
        unsafe { walk(self.as_ptr(), (&raw mut headers).cast(), on_header) };
        headers
    }

    fn headers(&self) -> Vec<(Vec<u8>, Vec<u8>)> {
        self.collect_headers(web_fetch_header_list_for_each)
    }

    // https://fetch.spec.whatwg.org/#concept-header-list-sort-and-combine
    fn sort_and_combine(&self) -> Vec<(Vec<u8>, Vec<u8>)> {
        self.collect_headers(web_fetch_header_list_sort_and_combine)
    }
}

impl Drop for HeaderList {
    fn drop(&mut self) {
        // SAFETY: The value owns one reference to the list.
        unsafe { web_fetch_header_list_unref(self.as_ptr()) };
    }
}

/// The union HeadersInit of Fetch/Headers.idl, whose members the bindings convert.
pub(crate) enum HeadersInit {
    Sequence(Vec<Vec<ByteString>>),
    Record(Vec<(ByteString, ByteString)>),
}

// https://fetch.spec.whatwg.org/#headers-guard
// A headers guard is "immutable", "request", "request-no-cors", "response" or "none".
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub(crate) enum Guard {
    Immutable,
    Request,
    RequestNoCors,
    Response,
    None,
}

/// The message of a TypeError that the algorithms of a Headers object throw.
pub(crate) type TypeErrorMessage = &'static str;

fn to_exception(message: TypeErrorMessage) -> Exception {
    Exception::TypeError(Utf16String::from_utf8(message))
}

#[derive(Trace)]
#[gc(crate = libjs_rust)]
pub(crate) struct Headers {
    // https://fetch.spec.whatwg.org/#concept-headers-header-list
    // A Headers object has an associated header list (a header list), which is initially empty.
    #[gc(untraced)]
    header_list: HeaderList,

    // https://fetch.spec.whatwg.org/#concept-headers-guard
    // A Headers object also has an associated guard, which is a headers guard. A headers guard is "immutable", "request", "request-no-cors", "response" or "none".
    #[gc(untraced)]
    guard: Cell<Guard>,
}

impl Wrappable for Headers {}

impl Headers {
    pub(crate) fn new(header_list: HeaderList) -> Self {
        Self {
            header_list,
            guard: Cell::new(Guard::None),
        }
    }

    pub(crate) fn header_list(&self) -> &HeaderList {
        &self.header_list
    }

    pub(crate) fn guard(&self) -> Guard {
        self.guard.get()
    }

    pub(crate) fn set_guard(&self, guard: Guard) {
        self.guard.set(guard);
    }

    // https://fetch.spec.whatwg.org/#dom-headers
    pub(crate) fn construct(_: &mut CallContext, init: Option<HeadersInit>) -> WebIdlResult<Self> {
        // SAFETY: A new header list is live, and the object adopts the reference that creating it returns.
        let headers = Self::new(unsafe { HeaderList::adopt(web_fetch_header_list_create()) });

        // The new Headers(init) constructor steps are:

        // 1. Set this’s guard to "none".
        headers.guard.set(Guard::None);

        // 2. If init is given, then fill this with init.
        if let Some(init) = init {
            headers.fill(init).map_err(to_exception)?;
        }

        Ok(headers)
    }

    // https://fetch.spec.whatwg.org/#dom-headers-append
    pub(crate) fn append(&self, _: &mut CallContext, name: ByteString, value: ByteString) -> WebIdlResult<()> {
        // The append(name, value) method steps are to append (name, value) to this.
        self.append_header(name.as_bytes(), value.as_bytes())
            .map_err(to_exception)
    }

    // https://fetch.spec.whatwg.org/#dom-headers-delete
    pub(crate) fn delete(&self, _: &mut CallContext, name: ByteString) -> WebIdlResult<()> {
        let name = name.as_bytes();

        // The delete(name) method steps are:

        // 1. If validating (name, ``) for headers returns false, then return.
        // NB: Passing a dummy header value ought not to have any negative repercussions.
        if !self.validate(name, b"").map_err(to_exception)? {
            return Ok(());
        }

        // 2. If this’s guard is "request-no-cors", name is not a no-CORS-safelisted request-header name, and name is not a privileged no-CORS request-header name, then return.
        if self.guard.get() == Guard::RequestNoCors
            && !is_no_cors_safelisted_request_header_name(name)
            && !is_privileged_no_cors_request_header_name(name)
        {
            return Ok(());
        }

        // 3. If this’s header list does not contain name, then return.
        if !self.header_list.contains(name) {
            return Ok(());
        }

        // 4. Delete name from this’s header list.
        self.header_list.delete(name);

        // 5. If this’s guard is "request-no-cors", then remove privileged no-CORS request-headers from this.
        if self.guard.get() == Guard::RequestNoCors {
            self.remove_privileged_no_cors_request_headers();
        }

        Ok(())
    }

    // https://fetch.spec.whatwg.org/#dom-headers-get
    pub(crate) fn get(&self, _: &mut CallContext, name: ByteString) -> WebIdlResult<Option<ByteString>> {
        // The get(name) method steps are:

        // 1. If name is not a header name, then throw a TypeError.
        if !is_header_name(name.as_bytes()) {
            return Err(to_exception("Invalid header name"));
        }

        // 2. Return the result of getting name from this’s header list.
        Ok(self.header_list.get(name.as_bytes()).map(ByteString::from_bytes))
    }

    // https://fetch.spec.whatwg.org/#dom-headers-getsetcookie
    pub(crate) fn get_set_cookie(&self, _: &mut CallContext) -> WebIdlResult<Vec<ByteString>> {
        // The getSetCookie() method steps are:

        // 1. If this’s header list does not contain `Set-Cookie`, then return « ».
        if !self.header_list.contains(b"Set-Cookie") {
            return Ok(Vec::new());
        }

        // 2. Return the values of all headers in this’s header list whose name is a byte-case-insensitive match for
        //    `Set-Cookie`, in order.
        Ok(self
            .header_list
            .headers()
            .into_iter()
            .filter(|(name, _)| name.eq_ignore_ascii_case(b"Set-Cookie"))
            .map(|(_, value)| ByteString::from_bytes(value))
            .collect())
    }

    // https://fetch.spec.whatwg.org/#dom-headers-has
    pub(crate) fn has(&self, _: &mut CallContext, name: ByteString) -> WebIdlResult<bool> {
        // The has(name) method steps are:

        // 1. If name is not a header name, then throw a TypeError.
        if !is_header_name(name.as_bytes()) {
            return Err(to_exception("Invalid header name"));
        }

        // 2. Return true if this’s header list contains name; otherwise false.
        Ok(self.header_list.contains(name.as_bytes()))
    }

    // https://fetch.spec.whatwg.org/#dom-headers-set
    pub(crate) fn set(&self, _: &mut CallContext, name: ByteString, value: ByteString) -> WebIdlResult<()> {
        let name = name.as_bytes();

        // The set(name, value) method steps are:

        // 1. Normalize value.
        let value = normalize(value.as_bytes());

        // 2. If validating (name, value) for headers returns false, then return.
        if !self.validate(name, value).map_err(to_exception)? {
            return Ok(());
        }

        // 3. If this’s guard is "request-no-cors" and (name, value) is not a no-CORS-safelisted request-header, then return.
        if self.guard.get() == Guard::RequestNoCors && !is_no_cors_safelisted_request_header(name, value) {
            return Ok(());
        }

        // 4. Set (name, value) in this’s header list.
        self.header_list.set(name, value);

        // 5. If this’s guard is "request-no-cors", then remove privileged no-CORS request-headers from this.
        if self.guard.get() == Guard::RequestNoCors {
            self.remove_privileged_no_cors_request_headers();
        }

        Ok(())
    }

    // https://fetch.spec.whatwg.org/#ref-for-dfn-value-pairs-to-iterate-over
    pub(crate) fn value_pair(&self, index: usize) -> Option<(ByteString, ByteString)> {
        // The value pairs to iterate over are the return value of running sort and combine with this’s header list.
        self.header_list
            .sort_and_combine()
            .into_iter()
            .nth(index)
            .map(|(name, value)| (ByteString::from_bytes(name), ByteString::from_bytes(value)))
    }

    // https://fetch.spec.whatwg.org/#headers-validate
    fn validate(&self, name: &[u8], value: &[u8]) -> Result<bool, TypeErrorMessage> {
        // To validate a header (name, value) for a Headers object headers:

        // 1. If name is not a header name or value is not a header value, then throw a TypeError.
        if !is_header_name(name) {
            return Err("Invalid header name");
        }
        if !is_header_value(value) {
            return Err("Invalid header value");
        }

        // 2. If headers’s guard is "immutable", then throw a TypeError.
        if self.guard.get() == Guard::Immutable {
            return Err("Headers object is immutable");
        }

        // 3. If headers’s guard is "request" and (name, value) is a forbidden request-header, then return false.
        if self.guard.get() == Guard::Request && is_forbidden_request_header(name, value) {
            return Ok(false);
        }

        // 4. If headers’s guard is "response" and name is a forbidden response-header name, then return false.
        if self.guard.get() == Guard::Response && is_forbidden_response_header_name(name) {
            return Ok(false);
        }

        // 5. Return true.
        Ok(true)
    }

    // https://fetch.spec.whatwg.org/#concept-headers-append
    pub(crate) fn append_header(&self, name: &[u8], value: &[u8]) -> Result<(), TypeErrorMessage> {
        // To append a header (name, value) to a Headers object headers, run these steps:

        // 1. Normalize value.
        let value = normalize(value);

        // 2. If validating (name, value) for headers returns false, then return.
        if !self.validate(name, value)? {
            return Ok(());
        }

        // 3. If headers’s guard is "request-no-cors":
        if self.guard.get() == Guard::RequestNoCors {
            // 1. Let temporaryValue be the result of getting name from headers’s header list.
            let temporary_value = match self.header_list.get(name) {
                // 2. If temporaryValue is null, then set temporaryValue to value.
                None => value.to_vec(),
                // 3. Otherwise, set temporaryValue to temporaryValue, followed by 0x2C 0x20, followed by value.
                Some(mut temporary_value) => {
                    temporary_value.extend_from_slice(b", ");
                    temporary_value.extend_from_slice(value);
                    temporary_value
                }
            };

            // 4. If (name, temporaryValue) is not a no-CORS-safelisted request-header, then return.
            if !is_no_cors_safelisted_request_header(name, &temporary_value) {
                return Ok(());
            }
        }

        // 4. Append (name, value) to headers’s header list.
        self.header_list.append(name, value);

        // 5. If headers’s guard is "request-no-cors", then remove privileged no-CORS request-headers from headers.
        if self.guard.get() == Guard::RequestNoCors {
            self.remove_privileged_no_cors_request_headers();
        }

        Ok(())
    }

    // https://fetch.spec.whatwg.org/#concept-headers-fill
    pub(crate) fn fill(&self, object: HeadersInit) -> Result<(), TypeErrorMessage> {
        // To fill a Headers object headers with a given object object, run these steps:
        match object {
            // 1. If object is a sequence, then for each header of object:
            HeadersInit::Sequence(object) => {
                for header in object {
                    // 1. If header's size is not 2, then throw a TypeError.
                    let [name, value] =
                        <[ByteString; 2]>::try_from(header).map_err(|_| "Array must contain header key/value pair")?;

                    // 2. Append (header[0], header[1]) to headers.
                    self.append_header(name.as_bytes(), value.as_bytes())?;
                }
            }
            // 2. Otherwise, object is a record, then for each key → value of object, append (key, value) to headers.
            HeadersInit::Record(object) => {
                for (key, value) in object {
                    self.append_header(key.as_bytes(), value.as_bytes())?;
                }
            }
        }
        Ok(())
    }

    // https://fetch.spec.whatwg.org/#concept-headers-remove-privileged-no-cors-request-headers
    fn remove_privileged_no_cors_request_headers(&self) {
        // To remove privileged no-CORS request-headers from a Headers object (headers), run these steps:

        const PRIVILEGED_NO_CORS_REQUEST_HEADER_NAMES: [&[u8]; 1] = [b"Range"];

        // 1. For each headerName of privileged no-CORS request-header names:
        for header_name in PRIVILEGED_NO_CORS_REQUEST_HEADER_NAMES {
            // 1. Delete headerName from headers’s header list.
            self.header_list.delete(header_name);
        }
    }
}
