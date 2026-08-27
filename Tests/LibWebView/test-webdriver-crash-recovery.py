#!/usr/bin/env python3
#
# Copyright (c) 2026-present, the Ladybird developers.
#
# SPDX-License-Identifier: BSD-2-Clause
#
# End-to-end coverage for WebContent crash recovery in a headed browser, where a crash leaves the
# replacement process dormant behind the chrome's crash overlay. Guards against the originally
# reported failure mode: a crashed tab whose history commands stall until they time out.
#   - a history command issued against the dormant tab completes and recovers the target entry
#   - the canonical session history survives the crash untouched
#   - a UI-initiated load from the dormant tab commits after the current entry, not over it
#   - a reload recovers the canonical current entry with a live document

import http.client
import http.server
import json
import socket
import subprocess
import sys
import threading
import time

# Well below the WebDriver request timeout, so a stalled history command fails the test with a
# readable error instead of hanging until the harness kills us.
PAGE_LOAD_TIMEOUT_MS = 30_000
REQUEST_TIMEOUT_SECONDS = 60
READINESS_TIMEOUT_SECONDS = 30


class TestPageHandler(http.server.BaseHTTPRequestHandler):
    slow_requested = threading.Event()
    release_slow = threading.Event()

    def do_GET(self):
        if self.path == "/slow":
            self.slow_requested.set()
            self.release_slow.wait(READINESS_TIMEOUT_SECONDS)
        body = f"<html><head><title>page{self.path}</title></head><body>Page {self.path}</body></html>".encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def request(webdriver_port, method, path, body=None, timeout=REQUEST_TIMEOUT_SECONDS):
    connection = http.client.HTTPConnection("127.0.0.1", webdriver_port, timeout=timeout)
    payload = json.dumps(body) if body is not None else None
    connection.request(method, path, payload, {"Content-Type": "application/json"})
    response = connection.getresponse()
    data = json.loads(response.read() or b"{}")
    connection.close()
    return response.status, data


def wait_for_webdriver(webdriver_port):
    deadline = time.monotonic() + READINESS_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        try:
            request(webdriver_port, "GET", "/status", timeout=2)
            return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError("Timed out waiting for WebDriver to start")


def current_url(webdriver_port, session_id):
    _, response = request(webdriver_port, "GET", f"/session/{session_id}/url")
    return response.get("value")


def ui_session_history(webdriver_port, session_id):
    _, response = request(webdriver_port, "GET", f"/session/{session_id}/ladybird/session-history")
    return response.get("value", {}).get("ui", {})


def ui_history_urls(webdriver_port, session_id):
    history = ui_session_history(webdriver_port, session_id)
    entries = history.get("entries", [])
    return [entry["url"] for entry in entries], [entry["url"] for entry in entries if entry.get("current")]


def wait_for_crash_overlay(webdriver_port, session_id, active):
    deadline = time.monotonic() + READINESS_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        try:
            if ui_session_history(webdriver_port, session_id).get("crashOverlayActive") == active:
                return
        except OSError:
            pass
        time.sleep(0.05)
    raise RuntimeError(f"Timed out waiting for crash overlay state {active}")


def crash_current_page(webdriver_port, session_id):
    request(
        webdriver_port,
        "POST",
        f"/session/{session_id}/ladybird/crash-current-page",
        {"waitForNavigationCompletion": False},
    )

    wait_for_crash_overlay(webdriver_port, session_id, True)


def document_text(webdriver_port, session_id):
    _, response = request(
        webdriver_port,
        "POST",
        f"/session/{session_id}/execute/sync",
        {"script": "return document.body.textContent", "args": []},
    )
    return response.get("value")


failures = []


def check(label, actual, expected):
    if actual != expected:
        failures.append(f"FAIL {label}: expected {expected!r}, got {actual!r}")
        print(failures[-1], flush=True)
    else:
        print(f"ok   {label}", flush=True)


def run_test(webdriver_binary):
    page_server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), TestPageHandler)
    threading.Thread(target=page_server.serve_forever, daemon=True).start()
    page_port = page_server.server_address[1]

    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        webdriver_port = probe.getsockname()[1]

    webdriver = subprocess.Popen([webdriver_binary, "-l", "127.0.0.1", "-p", str(webdriver_port)])

    try:
        wait_for_webdriver(webdriver_port)

        _, created = request(
            webdriver_port,
            "POST",
            "/session",
            {
                "capabilities": {
                    "alwaysMatch": {
                        "pageLoadStrategy": "normal",
                        "ladybird:enableTestHooks": True,
                        "timeouts": {"pageLoad": PAGE_LOAD_TIMEOUT_MS},
                    }
                }
            },
        )
        session_id = created.get("value", {}).get("sessionId")
        if not session_id:
            raise RuntimeError(f"Failed to create session: {created}")

        url_a = f"http://127.0.0.1:{page_port}/a"
        url_b = f"http://127.0.0.1:{page_port}/b"
        url_c = f"http://127.0.0.1:{page_port}/c"

        request(webdriver_port, "POST", f"/session/{session_id}/url", {"url": url_a})
        request(webdriver_port, "POST", f"/session/{session_id}/url", {"url": url_b})
        check("setup current url", current_url(webdriver_port, session_id), url_b)

        # The originally reported bug: after a crash the tab was permanently unusable, and history
        # commands stalled until they timed out. Back must complete and recover the target entry.
        crash_current_page(webdriver_port, session_id)
        entries, current = ui_history_urls(webdriver_port, session_id)
        check("canonical history preserved after crash", entries, [url_a, url_b])
        check("canonical current entry preserved after crash", current, [url_b])

        status, _ = request(webdriver_port, "POST", f"/session/{session_id}/back", {})
        check("back from crashed tab completes", status, 200)
        check("back from crashed tab recovers entry", current_url(webdriver_port, session_id), url_a)
        check("back from crashed tab loads document", document_text(webdriver_port, session_id), "Page /a")
        wait_for_crash_overlay(webdriver_port, session_id, False)
        check("back clears crash overlay", ui_session_history(webdriver_port, session_id).get("crashOverlayActive"), False)

        status, _ = request(webdriver_port, "POST", f"/session/{session_id}/forward", {})
        check("forward completes after recovery", status, 200)
        check("forward reaches expected entry", current_url(webdriver_port, session_id), url_b)

        # A browser-side WebDriver load from the dormant replacement process must commit after the canonical
        # current entry rather than replacing it, keeping the crashed entry reachable via Back.
        crash_current_page(webdriver_port, session_id)
        request(webdriver_port, "POST", f"/session/{session_id}/url", {"url": url_c})
        check("load from crashed tab commits", current_url(webdriver_port, session_id), url_c)
        wait_for_crash_overlay(webdriver_port, session_id, False)
        check("load clears crash overlay", ui_session_history(webdriver_port, session_id).get("crashOverlayActive"), False)
        entries, current = ui_history_urls(webdriver_port, session_id)
        check("load from crashed tab appends", entries, [url_a, url_b, url_c])

        status, _ = request(webdriver_port, "POST", f"/session/{session_id}/back", {})
        check("crashed entry reachable via back", current_url(webdriver_port, session_id), url_b)

        # A reload from the dormant state must repopulate the canonical current entry.
        crash_current_page(webdriver_port, session_id)
        status, _ = request(webdriver_port, "POST", f"/session/{session_id}/refresh", {})
        check("refresh from crashed tab completes", status, 200)
        check("refresh recovers canonical entry", current_url(webdriver_port, session_id), url_b)
        check("refresh recovers live document", document_text(webdriver_port, session_id), "Page /b")
        wait_for_crash_overlay(webdriver_port, session_id, False)
        check("refresh clears crash overlay", ui_session_history(webdriver_port, session_id).get("crashOverlayActive"), False)

        # If WebContent crashes before a new navigation commits, Reload retries that navigation rather than restoring
        # the previously committed page. The outstanding navigation request must also fail promptly on the crash.
        url_slow = f"http://127.0.0.1:{page_port}/slow"
        TestPageHandler.slow_requested.clear()
        TestPageHandler.release_slow.clear()
        request(
            webdriver_port,
            "POST",
            f"/session/{session_id}/ladybird/load-url-from-ui",
            {"url": url_slow, "waitForNavigationCompletion": False},
        )
        if not TestPageHandler.slow_requested.wait(READINESS_TIMEOUT_SECONDS):
            raise RuntimeError("Timed out waiting for the uncommitted navigation")

        crash_current_page(webdriver_port, session_id)
        TestPageHandler.release_slow.set()
        status, _ = request(webdriver_port, "POST", f"/session/{session_id}/refresh", {})
        check("reload retries the crashed navigation", status, 200)
        check("retried navigation reaches its target", current_url(webdriver_port, session_id), url_slow)
        wait_for_crash_overlay(webdriver_port, session_id, False)
        check("retried navigation clears crash overlay", ui_session_history(webdriver_port, session_id).get("crashOverlayActive"), False)
        entries, current = ui_history_urls(webdriver_port, session_id)
        check("retried navigation appends", entries, [url_a, url_b, url_slow])

        request(webdriver_port, "DELETE", f"/session/{session_id}")
    finally:
        webdriver.terminate()
        try:
            webdriver.wait(timeout=10)
        except subprocess.TimeoutExpired:
            webdriver.kill()
        page_server.shutdown()

    if failures:
        print(f"\n{len(failures)} failure(s)", flush=True)
        sys.exit(1)
    print("\nAll crash recovery checks passed", flush=True)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <webdriver-binary>", file=sys.stderr)
        sys.exit(1)
    run_test(sys.argv[1])
