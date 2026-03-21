#!/usr/bin/env python3

import argparse
import base64
import http.server
import json
import os
import posixpath
import socket
import socketserver
import sys
import time
import urllib.parse
from types import SimpleNamespace

from typing import Dict
from typing import Optional

"""
Description:
    This script starts a simple HTTP echo server on localhost for use in our in-tree tests.
    The port is assigned by the OS on startup and printed to stdout.

Endpoints:
    - POST /echo <json body>, Creates an echo response for later use. See "Echo" class below for body properties.
"""


class Echo:
    method: str
    path: str
    status: int
    headers: Dict[str, str]
    body: Optional[str]
    body_encoding: str
    delay_ms: Optional[int]
    reason_phrase: Optional[str]
    reflect_headers_in_body: bool

    def __eq__(self, other):
        if not isinstance(other, Echo):
            return NotImplemented

        return (
            self.method == other.method
            and self.path == other.path
            and self.status == other.status
            and self.body == other.body
            and self.body_encoding == other.body_encoding
            and self.delay_ms == other.delay_ms
            and self.headers == other.headers
            and self.reason_phrase == other.reason_phrase
            and self.reflect_headers_in_body == other.reflect_headers_in_body
        )


# In-memory store for echo responses
echo_store: Dict[str, Echo] = {}
WPT_IMPORT_PREFIX = "/Text/input/wpt-import/"


class TestHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    static_directory: str
    wpt_directory: Optional[str]
    wpt_source_directory: Optional[str]
    wpt_file_handler_cls = None
    wpt_python_handler_cls = None
    wpt_request_cls = None
    wpt_response_cls = None
    wpt_http_exception_cls = None

    def __init__(self, *arguments, **kwargs):
        super().__init__(*arguments, directory=self.static_directory, **kwargs)

    def _normalized_request_path(self):
        parsed_url = urllib.parse.urlsplit(self.path)
        return parsed_url, urllib.parse.unquote(parsed_url.path)

    def _is_echo_path(self):
        _, request_path = self._normalized_request_path()
        return request_path == "/echo" or request_path.startswith("/echo/")

    def _filesystem_path_for_url(self, root_directory, request_path):
        normalized_path = posixpath.normpath(request_path)
        parts = [part for part in normalized_path.split("/") if part and part not in (".", "..")]
        return os.path.join(root_directory, *parts)

    def _is_wpt_import_path(self, request_path):
        return request_path == WPT_IMPORT_PREFIX[:-1] or request_path.startswith(WPT_IMPORT_PREFIX)

    def _wpt_relative_path(self, request_path):
        if not self._is_wpt_import_path(request_path):
            return None

        suffix = request_path[len(WPT_IMPORT_PREFIX.rstrip("/")) :]
        return suffix or "/"

    def _wpt_filesystem_path_for_url(self, root_directory, request_path):
        relative_path = self._wpt_relative_path(request_path)
        if relative_path is None:
            return None
        return self._filesystem_path_for_url(root_directory, relative_path)

    def _path_has_default_python_handler(self, filesystem_path):
        if not os.path.isdir(filesystem_path):
            return False

        try:
            return any(
                os.path.isfile(os.path.join(filesystem_path, filename))
                for filename in sorted(os.listdir(filesystem_path))
                if filename.startswith("default") and filename.endswith(".py")
            )
        except OSError:
            return False

    def _locate_wpt_resource(self, request_path):
        for root_directory in (self.wpt_directory, self.wpt_source_directory):
            if not root_directory:
                continue

            filesystem_path = self._wpt_filesystem_path_for_url(root_directory, request_path)
            if filesystem_path is None:
                continue

            if (
                os.path.exists(filesystem_path)
                or os.path.isfile(filesystem_path + ".headers")
                or os.path.isfile(filesystem_path + ".sub.headers")
                or self._path_has_default_python_handler(filesystem_path)
            ):
                return root_directory, filesystem_path

        return None, None

    def _serve_wpt_request(self, request_path):
        if self.wpt_request_cls is None or self.wpt_response_cls is None or self.wpt_http_exception_cls is None:
            return False

        root_directory, filesystem_path = self._locate_wpt_resource(request_path)
        if root_directory is None or filesystem_path is None:
            return False

        relative_path = self._wpt_relative_path(request_path)
        if relative_path is None:
            return False

        if filesystem_path.endswith(".py") or self._path_has_default_python_handler(filesystem_path):
            handler = self.wpt_python_handler_cls(base_path=root_directory, url_base="/")
        else:
            handler = self.wpt_file_handler_cls(base_path=root_directory, url_base="/")

        original_path = self.path
        original_directory = self.directory
        query_suffix = f"?{urllib.parse.urlsplit(original_path).query}" if urllib.parse.urlsplit(original_path).query else ""

        try:
            self.path = relative_path + query_suffix
            self.directory = root_directory

            request = self.wpt_request_cls(self)
            request.doc_root = root_directory
            request.url_base = "/"
            response = self.wpt_response_cls(self, request)

            try:
                handler(request, response)
            except self.wpt_http_exception_cls as exception:
                response.set_error(exception.code, exception)
            except Exception as exception:
                response.set_error(500, exception)

            if not response.writer.content_written:
                response.write()

            self.close_connection = self.close_connection or response.close_connection
            return True
        finally:
            self.path = original_path
            self.directory = original_directory

    def _set_static_headers(self, headers_path):
        if not os.path.isfile(headers_path):
            return

        self._extra_headers = []
        with open(headers_path) as f:
            for line in f:
                line = line.strip()
                if ":" in line:
                    key, _, value = line.partition(":")
                    self._extra_headers.append((key.strip(), value.strip()))

    def _serve_static_request(self, request_handler):
        parsed_url, request_path = self._normalized_request_path()

        if request_path.startswith("/static/"):
            request_path = request_path[7:]

        if self._is_wpt_import_path(request_path):
            return self._serve_wpt_request(request_path)

        if request_handler is None:
            return False

        for root_directory in (self.static_directory,):
            if not root_directory:
                continue

            filesystem_path = self._filesystem_path_for_url(root_directory, request_path)
            headers_path = filesystem_path + ".headers"
            if os.path.exists(filesystem_path) or os.path.isfile(headers_path):
                self._set_static_headers(headers_path)
                query_suffix = f"?{parsed_url.query}" if parsed_url.query else ""
                self.directory = root_directory
                self.path = request_path + query_suffix
                request_handler()
                return True

        return False

    def end_headers(self):
        if hasattr(self, "_extra_headers"):
            for key, value in self._extra_headers:
                self.send_header(key, value)
            del self._extra_headers
        super().end_headers()

    def do_GET(self):
        if self._is_echo_path():
            self.handle_echo()
        else:
            if not self._serve_static_request(super().do_GET):
                self.send_error(404, "Not Found")

    def do_POST(self):
        _, request_path = self._normalized_request_path()
        if request_path == "/echo":
            content_length = int(self.headers["Content-Length"])
            post_data = self.rfile.read(content_length)
            data = json.loads(post_data.decode("utf-8"))

            echo = Echo()
            echo.method = data.get("method", None)
            echo.path = data.get("path", None)
            echo.status = data.get("status", None)
            echo.body = data.get("body", None)
            echo.body_encoding = data.get("body_encoding", "raw")
            echo.delay_ms = data.get("delay_ms", None)
            echo.headers = data.get("headers", {})
            echo.reason_phrase = data.get("reason_phrase", None)
            echo.reflect_headers_in_body = data.get("reflect_headers_in_body", False)

            is_invalid_echo_path = echo.path is None or not echo.path.startswith("/echo/")

            # Return 400: Bad Request if invalid params are given or a reserved path is given
            if (
                echo.method is None
                or echo.path is None
                or echo.status is None
                or echo.body_encoding not in ("raw", "base64")
                or (echo.body is not None and "$HEADERS" not in echo.body and echo.reflect_headers_in_body)
                or is_invalid_echo_path
            ):
                self.send_response(400)
                self.send_header("Content-Type", "text/plain")
                self.end_headers()
                return

            # Return 409: Conflict if the method+path combination already exists
            key = f"{echo.method} {echo.path}"
            if key in echo_store and echo_store[key] != echo:
                self.send_response(409)
                self.send_header("Content-Type", "text/plain")
                self.end_headers()
                message = (
                    "Echo already exists for method+path, but with a different definition.\n"
                    f"key: {key}\n"
                    "Hint: Use a unique path per test run (or keep the same definition).\n"
                )
                self.wfile.write(message.encode("utf-8"))
                return

            echo_store[key] = echo

            host = self.headers.get("host", "localhost")
            path = echo.path.lstrip("/")
            fetch_url = f"http://{host}/{path}"

            # The params to use on the client when making a request to the newly created echo endpoint
            fetch_config = {
                "method": echo.method,
                "url": fetch_url,
            }

            self.send_response(201)
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(fetch_config).encode("utf-8"))
        elif self._is_echo_path():
            self.handle_echo()
        else:
            if not self._serve_static_request(None):
                self.send_error(405, "Method Not Allowed")

    def do_OPTIONS(self):
        if self._is_echo_path():
            self.send_response(204)
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "*")
            self.send_header("Access-Control-Allow-Headers", "*")
            self.end_headers()
        else:
            if not self._serve_static_request(None):
                self.do_other()

    def do_PUT(self):
        self.do_other()

    def do_HEAD(self):
        if self._is_echo_path():
            self.handle_echo()
        else:
            if not self._serve_static_request(super().do_HEAD):
                self.send_error(404, "Not Found")

    def do_DELETE(self):
        self.do_other()

    def handle_echo(self):
        method = self.command.upper()
        key = f"{method} {self.path}"

        is_revalidation_request = "If-Modified-Since" in self.headers
        send_not_modified = is_revalidation_request and "X-Ladybird-Respond-With-Not-Modified" in self.headers

        send_incomplete_response = "X-Ladybird-Respond-With-Incomplete-Response" in self.headers

        set_invalid_cookie = "X-Ladybird-Set-Invalid-Cookie" in self.headers

        if key in echo_store:
            echo = echo_store[key]
            response_headers = echo.headers.copy()

            if echo.delay_ms is not None:
                time.sleep(echo.delay_ms / 1000)

            if send_not_modified:
                self.send_response(304)
            else:
                self.send_response_only(echo.status, echo.reason_phrase)

                if is_revalidation_request:
                    # Override the Last-Modified header to prevent cURL from thinking the response is still fresh.
                    response_headers["Last-Modified"] = "Thu, 01 Jan 1970 00:00:00 GMT"
                elif send_incomplete_response:
                    # We emulate an incomplete response by advertising a 10KB file, but only sending 2KB.
                    response_headers["Content-Length"] = str(10 * 1024)

            if set_invalid_cookie:
                response_headers["Set-Cookie"] = "invalid=foo; Domain=\xc3\xa9\x6c\xc3\xa8\x76\x65\xff"

            # Set only the headers defined in the echo definition
            if response_headers:
                for header, value in response_headers.items():
                    self.send_header(header, value)
                self.end_headers()

            if send_not_modified:
                return

            if send_incomplete_response:
                self.wfile.write(b"a" * (2 * 1024))
                self.wfile.flush()

                self.connection.shutdown(socket.SHUT_WR)
                self.connection.close()
                return

            if echo.reflect_headers_in_body:
                headers = {}
                for key in self.headers.keys():
                    headers[key] = self.headers.get_all(key)
                headers = json.dumps(headers)
                response_body = echo.body.replace("$HEADERS", headers) if echo.body else headers
            else:
                response_body = echo.body or ""

            # FIXME: This only supports "Range: bytes=start-end" and "Range: bytes=start-". There are other formats to
            #        support if needed: https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Range#syntax
            if "Range" in self.headers:
                range_value = self.headers["Range"].strip()
                assert range_value.startswith("bytes=")
                assert range_value.count("-") == 1

                range_value = range_value[len("bytes=") :]
                start, end = range_value.split("-")

                if end:
                    response_body = response_body[int(start) : min(int(end), len(response_body))]
                else:
                    response_body = response_body[int(start) :]

            if echo.body_encoding == "base64":
                self.wfile.write(base64.b64decode(response_body))
            else:
                self.wfile.write(response_body.encode("utf-8"))
        else:
            self.send_error(404, f"Echo response not found for {key}")

    def do_other(self):
        if self._is_echo_path():
            self.handle_echo()
        else:
            if not self._serve_static_request(None):
                self.send_error(405, "Method Not Allowed")


def start_server(port, static_directory, wpt_directory):
    TestHTTPRequestHandler.static_directory = os.path.abspath(static_directory)
    TestHTTPRequestHandler.wpt_directory = os.path.abspath(wpt_directory) if wpt_directory else None
    TestHTTPRequestHandler.wpt_source_directory = os.path.join(TestHTTPRequestHandler.static_directory, "WPT", "wpt")
    httpd = socketserver.TCPServer(("127.0.0.1", port), TestHTTPRequestHandler)
    httpd.scheme = "http"
    httpd.router = SimpleNamespace(doc_root=TestHTTPRequestHandler.static_directory)

    wpt_stash_server = None
    if TestHTTPRequestHandler.wpt_directory and os.path.isdir(TestHTTPRequestHandler.wpt_source_directory):
        wpt_tools_directory = os.path.join(TestHTTPRequestHandler.wpt_source_directory, "tools")
        if wpt_tools_directory not in sys.path:
            sys.path.insert(0, wpt_tools_directory)

        import localpaths  # noqa: F401
        from wptserve import handlers as wpt_handlers
        from wptserve.config import Config
        from wptserve.request import Request, Server
        from wptserve.response import Response
        from wptserve.stash import StashServer
        from wptserve.utils import HTTPException

        config = Config(
            {
                "browser_host": "localhost",
                "ports": {
                    "http": [httpd.socket.getsockname()[1], 0],
                    "https": [0],
                    "ws": [0],
                    "wss": [0],
                },
                "all_domains": {
                    "": "localhost",
                    "www1": "www1.localhost",
                    "www2": "www2.localhost",
                    "alt": "localhost",
                },
                "logging": {
                    "suppress_handler_traceback": False,
                },
            }
        )

        Server.config = config
        TestHTTPRequestHandler.wpt_file_handler_cls = wpt_handlers.FileHandler
        TestHTTPRequestHandler.wpt_python_handler_cls = wpt_handlers.PythonScriptHandler
        TestHTTPRequestHandler.wpt_request_cls = Request
        TestHTTPRequestHandler.wpt_response_cls = Response
        TestHTTPRequestHandler.wpt_http_exception_cls = HTTPException

        wpt_stash_server = StashServer()
        wpt_stash_server.__enter__()

    print(httpd.socket.getsockname()[1])
    sys.stdout.flush()

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if wpt_stash_server is not None:
            wpt_stash_server.__exit__(None, None, None)
        httpd.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run a HTTP echo server")
    parser.add_argument(
        "-d",
        "--directory",
        type=str,
        default=".",
        help="Directory to serve static files from",
    )
    parser.add_argument(
        "-p",
        "--port",
        type=int,
        default=0,
        help="Port to run the server on",
    )

    args = parser.parse_args()

    start_server(
        port=args.port,
        static_directory=args.directory,
        wpt_directory=os.path.join(args.directory, "Text", "input", "wpt-import"),
    )
