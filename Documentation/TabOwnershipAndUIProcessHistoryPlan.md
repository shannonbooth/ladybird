# UI-process history, close, and tab ownership: full plan (revision 2)

Implementation status in `codex/tab-ownership` (2026-11-05):
- Based on `04d8e0def61`, including all Part A prerequisites.
- Part F.2 implemented: an owning set, view-independent lookup and iteration, and removal through `destroy()`.
- Part F.3 foundation implemented: traversable-owned session, `TabId`, WebDriver handle/BiDi identity,
  owner tab identity, worker/performance attribution, and one-time permission lifetime.
- WebDriver listing/routing without a view still needs Part F.6. Close notifications remain at the existing sites,
  as required until F.8. `destroy()` currently only performs set removal; its full E.2 algorithm is still pending.
- Part F.4 implemented: traversable-owned presentation, frontend observation, retained title/favicon/loading,
  and aggregated audibility and blocked-resource totals.
- Parts F.5–F.9 and the independent Part G work are not implemented.
- Validation: built LibWebView and the standalone `TestTraversableOwnership` target; all five ownership/presentation tests pass.

Status: 2026-11-05. The branch is `history-population-request-in-ui`, based on upstream `9a3e6df0f14`. Nothing is pushed.
Revision 2 takes in review points 1–6 (destruction ordering, bounded close, request tokens, WebDriver identity, the
`window.close()` split, preflight adoption) and the reviewer's answers to the open questions.

The goal is the spec's algorithm shapes, with the UI process as the authority over state it owns. Renderers only run what
needs the DOM or script: firing events, creating documents and unloading.

---

## Part A. Already on the branch

1. **`45b3fce7d47` LibWeb+LibWebView+WebContent: Build history population requests in UI**
   - The renderer used to build the history population request and echo it back with the result. The UI's
     NavigationLoader determined the document's origin from those echoed fields (srcdoc origin, initiator origin).
   - Now the UI decides step 12.8 when it dispatches the job and sends `needs_population`. The renderer reaches step 8
     and replies `changing_navigable_history_job_reached_population` with a snapshot of its active document. The UI then
     runs 12.8.1–7: it takes the target snapshot, picks the source snapshot, clears reload pending, computes allowPOST,
     builds the request and the loader, and sends `create_history_navigation_params`. The renderer returns only the
     result.
   - A `ChangingNavigable::population` Variant (`AwaitingPopulation`, `CreatingNavigationParams`,
     `AcquiringResponseBody`, `Populating`) guards each reply.
   - `ChangingNavigable::target_document(job_page)` is the one notion of targetEntry's document.
   - Pitfall: a javascript: URL's populated document has no host, so a populated document counts as present whatever
     its host.
2. **`d51e087088f` …: Run afterDocumentPopulated in UI**
   - `changing_navigable_history_job_ready` no longer carries `target_document_id`. `update_only` means only "step 12.4
     said so" or "population created no document".
   - The UI decides afterDocumentPopulated steps 2–3 and applies the population result plus steps 2–3 to its canonical
     entry at activation. That is whatwg/html issue 9869: a reload's target entry is the active entry, which the active
     document and URL are derived from.
   - A replacement document state takes over the populated document.
   - The renderer applies steps 2–3 to its own copy, from two bools on the continuation messages.
   - This fixes a bug where history repopulation results never reached the UI's entry.
3. **`012c3980807` …: Run history jobs once across processes**
   - When the populated document's host isn't the job's page, the UI sends `populate_history_entry_document`. That
     process claims the entry, builds the continuation and creates the document. It doesn't re-run steps 12.1–12.7.
   - `continue_history_navigation_population` is resume-only.
4. **`8b14a249470` …: Set opener browsing contexts in UI**
   - `window.open` step 16.2 asks the UI, which checks that the requester hosts the source, sets the canonical opener
     and pushes it to the host.
   - `window.opener = null` reports only `did_disown_opener_browsing_context`.
   - This fixes the opener being dropped silently when the target was in another tab. One isolation test was updated
     to the spec behaviour.
5. **`04d8e0def61` LibWebView: Keep a traversable's cookie versions on the traversable**
   - The document cookie version buffer and its indices move from the view to `CanonicalTraversable`.
   - Pages read the browsing session through `client().session()`.

Other state:
- Stash `close-in-ui-wip`: the UI half of "close in the UI", to be reworked under Parts D–E.
- Stash `ever-populated-ui-wip` and branch `backup/redirect-document-state-in-ui`: discarded work, described in
  "Rejected" below.
- Backups: `backup/history-population-request-in-ui-v1` and `-v2`.
- **The uncommitted null-check version of "WebContentPage works with no view" in another session is superseded and
  should be dropped.**

### Agreed mapping of apply-the-history-step step 12 (reference)
- **WebContent, at task time:** 12.1 (displayedEntry), 12.4 (update-only), 12.7 (traverse navigate event). These
  compare displayedEntry, which synchronous navigations move in the renderer first. 12.8.4's snapshot of the active
  document is also taken there.
- **UI:** 12.2, 12.5 (the renderer can keep asserting on UI-sent data), 12.6, the 12.8 condition, 12.8.1–7, and
  afterDocumentPopulated.
- **Step 14:** UI. "Queue a global task … afterPotentialUnloads" is the apply message.

### Rejected (do not redo)
- **The UI building redirect replacement document states.** It moved steps 17–20 out of the fetch loop. It waits for
  the Fetch move into the UI.
- **Deleting the renderer's `ever_populated` / `user_agent_initiated` fields.** The UI-only alternative is still open,
  see Part G.
- **Interim trust checks on renderer-reported navigation responses.** These wait for the Fetch move.

---

## Part B. Ownership model (target)

1. The user agent's top-level traversable set owns every `CanonicalTraversable`. Only destroying it removes it, and
   destruction is always bounded in time (Part E.3).
2. A traversable's user interface is a property of the traversable (`TabUserInterface`). It may have no view.
3. A `WebContentPage` is one process's endpoint for one traversable. It routes to the traversable, its navigables, its
   user interface, or the client's session, and never to a view.
4. Profile state (cookies, permissions, storage, history) belongs to `BrowsingSession`.
5. Removing the UI is a request to close the traversable. Explicit frontend closes and transfers say so up front; the
   view destructor's close is only a fallback. Destroying the traversable removes it from the UI.
6. Identity that the web platform and automation see (`TabId`, the WebDriver window handle, the BiDi context id)
   belongs to the traversable and survives detaching a view.

### Naming rule
- `Canonical*`: the UI's authoritative copies of spec objects.
- `Tab*`: per-tab UI concerns.
- `Local*` / `Remote*`: the renderer's copies and proxies.

---

## Part C. Types

**`BrowsingSession`** (exists) is the profile. Pages reach it through their client, and traversables hold a strong
reference, so a private session lives as long as its traversables.

**`UserAgentTopLevelTraversableSet`** (new class; today a static vector in `CanonicalTraversable.cpp`)
- Owns `NonnullOwnPtr<CanonicalTraversable>`: create, lookup, iteration.
- `remove(traversable)` is reached only from `CanonicalTraversable::destroy()` (Part E.2).

**`CanonicalTraversable`** (keeps its name; it is always top-level)
- **Owns:**
  - the session history, the traversal queue and the navigable tree;
  - session storage and document cookie versions;
  - "is closing", its pages;
  - `TabUserInterface` and `TabPresentationState`;
  - `TabId`, the WebDriver window handle and the BiDi context id;
  - a reference to its `BrowsingSession`;
  - the close deadline;
  - a destruction generation that queued UI-side tasks check.
- **Holds:** a non-owning `TabView*`, cleared together with the view's back pointer (Part E.1).
- **Spec algorithms:** close a top-level traversable; definitely close; destroy a top-level traversable; checking if
  unloading is canceled, as an operation object (`UnloadCheckOperation`, below).
- **`attach_view(TabView&)` / `detach_view()`** don't change the traversable's lifetime.
- **`user_interface_went_away()`** is the fallback policy point used by the view destructor. It closes without
  prompting, unless the traversable is already destroyed. Moves between windows don't go through it: they keep the
  view, as Qt tab moves do today.
- **Rename `display_page()` → `top_level_document_host()`.**

**`UnloadCheckOperation`** (new; the UI's "checking if unloading is canceled")
- An id, the documents still to check, the document currently prompting, the purpose (navigation, history traversal,
  close, close preflight), and completion steps.
- Dispatches `run_beforeunload_check` to each hosting process in turn, with the prompt shown at most once.
- Can be adopted by a close (Part E.6).

**`TabUserInterface`** (new, concrete, owned by the traversable)
- Every request takes the requesting `WebContentPage&`, and every reply goes to that page, not the view's page.
- With a view, it forwards to the view. Without one, it gives the no-UI answer.
- **Outstanding requests live in its own typed table,** which shares page-close notification with `WebContentPage`.
  It is separate from `OwedReply`, because `OwedReply` tracks replies owed in the opposite direction, by the renderer.
  - Each entry is `{ token, kind, requesting page, document id (where authorisation depends on it), reply }`.
  - Requests to the view carry the token, and the view's completions return it.
  - **Cancelling an entry removes it from the table before its reply runs.** A completion whose token isn't in the
    table is ignored, so a picker cancelled by a detach can't answer a later picker.
  - Completions for permission and geolocation also check that the document id is still the requesting page's
    document.
  - Page loss settles only that page's entries. Detaching settles all of them with the no-UI answer.
  - "One dialog of a kind at a time" is a query on the table (today `another_page_awaits` in the view).
  - An open `beforeunload` dialog's entry records the `UnloadCheckOperation` it belongs to.
- **Request groups and their no-UI answers:**
  - **Dialogs:** alert closed, confirm false, prompt null, `beforeunload` accepted.
  - **Pickers** (colour, date, file, select dropdown): cancelled.
  - **Prompts** (permission, geolocation request/watch/cancel, microphone authorisation): denied or position
    unavailable.
  - **Windows** (restore, move, resize, maximize, minimize, fullscreen, exit fullscreen, system visibility, new window,
    picture-in-picture window): complete the rect request, deny fullscreen, no page.
  - **Navigation chrome** (open in new tab and its input activation, external URL, link click, middle click): dropped.
  - **Text services** (spell check, writing suggestions, clipboard read/insert): empty replies.
  - **Presentation:** cursor, tooltips and link hover are dropped. Document presentation updates go to
    `TabPresentationState`.

**`TabPresentationState`** (new, on the traversable)
- **Document presentation only:** title, URL, favicon, load progress, audibility, blocked-resource counts.
- **Update sources:**
  - title, favicon, URL and load progress come from the top-level document's host;
  - audibility is aggregated over every hosting page;
  - blocked-resource counts are summed over every hosting page.
- The view observes it, and attaching a view renders it.
- **Editing state stays in the frontend:** an unsubmitted address-bar value, find-bar text, selection in chrome.

**`TabView`** (renamed from `ViewImplementation`)
- **Display:** backing stores, compositor context, viewport size, device pixel ratio, fullscreen.
- **Input:** routes to the focused navigable's page, grants activation, keeps input accounting (including events
  forwarded to child frames).
- **The UI side of `TabUserInterface`:** token-carrying calls become the frontend's `on_*` hooks.
- **Per-window chrome:** find in page, debugger, spelling menu, geometry.
- **Forwards user requests to the traversable:** navigate, reload, back, forward, stop; close, force close and close
  preflight, which say explicitly whether to prompt.
- **Pages get narrow capabilities, never the whole view:**
  - `TabInputChannel`: input completion and forwarding to child frames;
  - `TabAutomation`: view-dependent WebDriver command execution (input, screenshots, viewport) and testing hooks.

  Both are absent with no view.
- **Destructor (Part E.1):** enter the no-UI state, then call `user_interface_went_away()`, then detach both pointers.
  It never frees the traversable or closes its pages.
- Frontend subclasses (Qt `WebContentView`, AppKit, `HeadlessWebView`) keep their names and subclass `TabView`.

**`WebContentPage`** (keeps its name)
- **No `view()` accessor.**
- `displays_tab()` is removed in favour of `hosts_active_document_of(traversable())`, or a short
  `hosts_top_level_document()`.

**`WebContentClient`**
- Answers process-level questions through traversables and sessions: page-may-capture-audio, microphone
  authorisation, owner ids, embedded-page setup.
- Can close one traversable's pages without shutting down a process that hosts other tabs (Part E.3).

### Keys that move from view to traversable
- `owner_view_id` in the performance monitor, worker owners and audio capture becomes `TabId`.
- One-time permission grants (`PermissionManager::remove_view`) end with the traversable.
- **WebDriver:**
  - the window handle and the BiDi context id are the traversable's;
  - "window closed" and "context destroyed" fire at destroy, not at detach;
  - close-command completion is the traversable's;
  - with no view, a command that needs one fails with "no such window", but the window is still open and still
    listed.

---

## Part D. Lifecycle

1. **Created:** the set creates the traversable with a session, a `TabId`, a handle and no view. It starts in the
   no-UI state, which covers unattached popups and headless children.
2. **Attached:** the frontend creates a `TabView` and attaches it. `TabPresentationState` is rendered.
3. **Running.**
4. **Close requested** (Part E.4–E.6):
   - **script:** `window.close()`, split as in Part E.5;
   - **user:** the view asks the traversable to close, prompting;
   - **forced:** without prompting;
   - **view destructor:** the fallback, without prompting.
5. **Definitely close** in the UI:
   - prompting closes run the `UnloadCheckOperation` across every hosting process. That fixes the missing
     `beforeunload` in isolated iframes;
   - the close is appended as a session history traversal step, and the close deadline starts;
   - the cross-process unload recursion runs;
   - then the host's unload-and-destroy task.
6. **Detached while closing:** the no-UI state answers every outstanding request. The traversable keeps closing; its
   documents unload, read cookies and get no-UI answers.
7. **Destroyed:** the host reports `did_close_browsing_context`, or the close deadline expires. Destroy follows the
   order in Part E.2.
8. **Process lost:**
   - with a view: crash recovery (view-driven, for now);
   - without a view: destroy, deferred, because `WebContentClient` keeps using the traversable for the rest of its
     loop.

---

## Part E. Lifecycle rules (normative)

### E.1 Detaching (both directions, synchronously)
`CanonicalTraversable::detach_view()` and `TabView`'s destructor both run `sever(view, traversable)`:
1. `TabUserInterface` enters the no-UI state. Every outstanding entry is removed from the table, then answered with
   its no-UI result, so no completion can reach the frontend afterwards.
2. Presentation observers are disconnected.
3. The traversable's `TabView*` and the view's traversable pointer are both cleared, in that one function.

The view destructor runs `sever` first, then `user_interface_went_away()`. Nothing it starts can call back into a
frontend that is being torn down.

### E.2 Destroying
`CanonicalTraversable::destroy()`:
1. If a view is attached, `sever` (E.1), then tell the frontend to remove the tab. A frontend that deletes the view
   later (Qt `deleteLater()`) finds a view with no traversable, and its destructor does nothing to it.
2. Bump the destruction generation. Queued UI-side tasks that captured the traversable check it, and weak pointers
   go null at step 6.
3. Fire "window closed" / "context destroyed" for WebDriver, and complete pending close commands.
4. Cancel the close deadline.
5. Release its pages: settle their owed replies and close any still open.
6. Remove it from the set, which frees it.

The removal is deferred when destroy is reached from inside a caller that keeps using the traversable, for example
`WebContentClient`'s lost-process loop. Steps 1–5 still run synchronously.

### E.3 Bounded close
- The deadline starts when definitely-close's steps are appended. The default is 5 seconds, and it is configurable;
  automation can set it.
- **On expiry**, destroy runs (E.2) with forced release: for each process hosting one of the traversable's pages,
  close those pages there (`close_page_of_closed_tab`) and settle their owed replies.
- A process that hosts other tabs keeps running for them. A stuck script in it is that process's problem, handled by
  its existing hang handling.
- A process left hosting nothing goes through the existing unused-process shutdown, whose precondition (all its pages
  closed) holds again after forced release.
- The traversable drops its `BrowsingSession` reference, so a private session left with no traversables is released.

### E.4 Close entry points
- `TabView::request_close()`: prompting close.
- `TabView::force_close()` and the frontend's "close without prompting" (WebDriver, Qt window close after the
  preflight): definitely close without prompting.
- `TabView::request_close_preflight(callback)`: an `UnloadCheckOperation` with purpose "close preflight". It doesn't
  close.
- **Picture-in-picture:** `PictureInPictureController` asks the UI to close without prompting.
- **The view destructor:** the fallback above.

### E.5 `window.close()`
- **The renderer runs steps 1–6 locally.** It checks that `thisTraversable` is a top-level traversable and not
  closing, then the three eligibility checks of step 6: script-closable, incumbent familiar with the browsing context,
  allowed by sandboxing to navigate.
  - Only if all pass does it set its local "is closing" (6.1), so `window.closed` is true at once, and send the request
    (6.2).
  - If any fails it does nothing, as today.
- **The UI re-runs step 6's checks as the authority**, with its own state: canonical sandboxing flags, UI activation,
  canonical familiarity and script-closability.
  - If they pass, it sets the canonical "is closing", replicated to other processes, and **queues** definitely-close
    as a task. That preserves the DOM manipulation task boundary: it never runs inside the request handler.
  - If they fail, the UI tells the host to reset its local "is closing" to false. That is the only reconciliation
    case, and it needs either a stale or a misbehaving renderer.
- When a close starts elsewhere (user, remote `window.close()`, forced), the UI sets the host's local "is closing" so
  `window.closed` agrees.
- If definitely-close's unload check is canceled, "is closing" is reset to false in the UI and in the host. That is the
  existing AD-HOC that allows a later close.

### E.6 Close preflight and adoption
First, port the responsibilities of today's renderer `adopt_for_close()` as written, then move them to the UI:
- **An `UnloadCheckOperation` for a navigation or traversal may be adopted by a close:**
  - the close takes over the operation id;
  - documents already checked count as checked;
  - the remaining documents are checked for the close;
  - the document currently prompting keeps its dialog, whose table entry is re-pointed at the close.
- **The adopted navigation or traversal completes as canceled by the close,** the same as a `beforeunload` "stay".
  Work queued for it is invalidated by the operation id.
- **Cancelling at the prompt** ends the close and the adopted operation as canceled.
- A preflight with no operation to adopt starts its own.
- An open dialog that isn't a `beforeunload` answers the preflight with "not approved", as today.

### E.7 Renderer side of close
- **Removed:**
  - the renderer's close and definitely-close steps, and `m_close_steps_have_been_appended`;
  - `page_did_request_unload_check` / `request_unload_check` / `unload_check_result` and `m_pending_unload_checks`;
  - the IPCs `close_traversable_from_script`, `request_close`, `force_close`, `request_close_without_prompting` and
    `request_close_preflight`;
  - `HostedNavigableState::is_closing`;
  - renderer-side `adopt_for_close`, after its port (E.6).
- **Added:** `set_top_level_traversable_closing(bool)` (UI → host).
- **Kept:** `run_traversable_close_unload_task`; the host's local "is closing" for `window.closed`.
- A renderer requesting `CloseTopLevelTraversableHistoryOperationParameters` is refused as misbehaviour. Only the UI
  enqueues it.

---

## Part F. Commit series (remaining)

1. *(done)* `04d8e0def61`.
2. **`UserAgentTopLevelTraversableSet` as a class.** Creation goes through it, and removal goes through it from one
   place. No behaviour change.
3. **Traversable identity:** `TabId`, the `BrowsingSession` reference, and the WebDriver handle and BiDi context id
   moving from the view. `owner_view_id` users and one-time permission grants move to `TabId`. WebDriver close events
   stay where they fire today until commit 8.
4. **`TabPresentationState`**, with the update sources above. The view observes it.
5. **`TabUserInterface` with the token table.** Every UI request is routed through it, with requesting-page replies
   and token-carrying completions. Behaviour with a view is unchanged. "One dialog at a time" moves in.
6. **The no-UI state:**
   - the no-UI answers, settling on page close;
   - `TabInputChannel` and `TabAutomation`;
   - `WebContentPage::view()` and `displays_tab()` removed;
   - outside callers fixed: `WebContentClient`, `NotificationManager`, `TabPerformanceMonitor`,
     `WorkerProcessManager`, WebDriver BiDi, headless picture-in-picture.
7. **`UnloadCheckOperation` in the UI**, used by navigation, history traversal and the existing close relay. Port
   `adopt_for_close` (E.6) as written.
8. **Lifetime and close together** (they can't land apart, because the destructor's close needs UI-owned close):
   - attach/detach with `sever` (E.1);
   - `destroy()` ordering (E.2);
   - the bounded close (E.3);
   - the close entry points (E.4);
   - `window.close()` (E.5);
   - preflight and adoption in the UI (E.6);
   - the renderer removals (E.7);
   - WebDriver close events at destroy.

   Builds on the stashed close work.
9. **Renames:** `ViewImplementation` → `TabView`, `display_page()` → `top_level_document_host()`. Mechanical, so last.

### Tests to add
- **LibWebView unit tests:**
  - no-UI answers for a dialog, picker, permission request, `window.open` and clipboard read;
  - detaching settles outstanding requests;
  - a stale token's completion after detach and reattach is ignored, and doesn't answer the new picker;
  - page loss settles only that page's requests;
  - destroy with a view still attached, then deleting the view later (a `deleteLater()` simulation), doesn't touch
    freed memory;
  - the close deadline expires on a host that never acknowledges, with the traversable destroyed and the private
    session released.
- **test-web:**
  - a close without prompting whose `unload` handler calls `alert`, reads `document.cookie` and sends a beacon. It
    must finish, and the beacon must arrive;
  - an `unload` handler that loops forever: the tab still goes away after the deadline, and another tab in the same
    process keeps working;
  - tab close with `beforeunload` in an isolated iframe;
  - detaching while a `beforeunload` dialog is open;
  - `window.close()` from same-process and isolated frames: `window.closed` is true immediately after a successful
    call, and false after a rejected one;
  - a close that adopts a navigation's open `beforeunload` dialog: the navigation ends canceled;
  - WebDriver close with and without `promptUnload`; the handle stays listed while a detached traversable is closing;
  - picture-in-picture close;
  - a headless child force-close.

---

## Part G. Other planned work (independent of Parts C–F)

1. **Collapse the navigate handshake.**
   - Navigate step 23 becomes one UI-run in-parallel block: check if unloading is canceled (using
     `UnloadCheckOperation`), queue the abort task, document state and history entry, populate.
   - Removes `set_ongoing_navigation`, `run_navigation_unload_check`, `did_complete_navigation_unload_check` and, on the
     plain path, `create_navigation_params`.
   - It touches every navigation, so plan it in detail first.
2. **History trust gaps** from the 2026-11-05 review:
   - check same-document entry URLs with "can have its URL rewritten" (LibWebCommon), including fragment navigation;
   - require `TraverseByDelta.initiator_to_check` and check that the requester hosts it; AND
     `has_transient_activation` with UI activation; hosting-check `NavigationAPITraverse.navigable_id`;
   - accept `ResumeTraverse` only for an intercepted traverse the UI recorded;
   - hosting-check `NavigableDestruction` and `NavigableCreation` ids;
   - clamp renderer user involvement: refuse `BrowserUI`, AND `Activation` with UI activation.
3. **The UI owns `ever_populated` / `user_agent_initiated`, UI-only version.**
   - The UI sets ever populated in `populate_document`, and copies `user_agent_initiated` in javascript: URL step 11.
   - The UI ignores both values in process updates to document states it already holds.
   - The renderer is unchanged.
   - The redirect replacement case stays renderer-reported until the Fetch move.
4. **`active_document_is_fully_active` derived in the UI**, dropped from `HostedNavigableState`.
5. **The Fetch move into the UI.** This is the structural fix for response trust, including redirect document states.
6. **Crash recovery from the view to the traversable.**
7. **The lazy-load stop in the history job's claim** uses `navigable->container()`, which is null for isolated iframes.
   Unverified; investigate.
8. **IPC review leftovers:**
   - merge the apply and unload-and-apply continuation messages;
   - fold `history_step_applied` and `reject_navigation_api_traversal` into `complete_history_operation`;
   - split the navigation-start message into two;
   - one failure channel for a dead navigation;
   - make reload pending have one writer;
   - unify `PendingSessionHistoryEntryDescriptor` and `SessionHistoryEntryDescriptor`.

---

## Decisions recorded from review
- `TabUserInterface` is one concrete policy-and-routing class, not a virtual interface.
- Outstanding UI requests use their own typed, token-keyed table, not `OwedReply` (opposite direction), sharing
  page-close notification.
- The view destructor's close without prompting is a fallback. Explicit close and transfer paths state their intent
  earlier, and moves between windows keep the view.
- Pages get narrow capabilities (`TabInputChannel`, `TabAutomation`), not an optional whole view.
- Document presentation lives on the traversable. Chrome editing state stays in the frontend.
- `TabId` (and WebDriver identity) comes before `TabUserInterface`. The lifetime transition and UI-owned close land
  together.

## Remaining open questions
1. What deadline should the bounded close use, and should a frontend be able to extend it (for example a "page is not
   responding" prompt)?
2. When the deadline force-releases pages in a shared process, should the UI also ask that process to interrupt the
   stuck script (as the hang monitor would), or leave that to the hang monitor?
3. Should `TabPresentationState` also carry the security indicator inputs (TLS state, mixed content), which today the
   view derives?
