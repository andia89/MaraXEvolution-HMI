#!/usr/bin/env python3
"""Serve one TFT file, trigger the ESP32 HMI updater, and wait for its result.

Python 3.9+, standard library only. See README.md for firmware installation.
The laptop and ESP32 must be able to reach each other on the local network.
"""

import argparse
import getpass
import ipaddress
import json
import math
import os
from pathlib import Path
import secrets
import shutil
import socket
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import ProxyHandler, Request, build_opener

MAX_SIZE = 128 * 1024 * 1024
MAX_TIMEOUT = 21600
BAUD = 115200
TERMINAL_STATES = {"success", "error", "timeout"}


class ApiError(RuntimeError):
    def __init__(self, code, message):
        super().__init__(f"ESP32 HTTP {code}: {message}")
        self.code = code


class UpdateFailed(RuntimeError):
    pass


def device_address(value):
    parsed = urlsplit(value if "://" in value else "http://" + value)
    if (parsed.scheme != "http" or not parsed.hostname or parsed.username or
            parsed.password or parsed.query or parsed.fragment or
            parsed.path not in ("", "/")):
        raise ValueError("--device must be an IP/hostname, optionally with :port (HTTP only)")
    try:
        ip = socket.gethostbyname(parsed.hostname)
        port = parsed.port or 8080
    except (ValueError, OSError) as exc:
        raise ValueError(f"Cannot resolve device {value!r}: {exc}") from exc
    return ip, port


def source_ip_for(device_ip):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        # Select the local route/address; UDP connect sends no application data.
        sock.connect((device_ip, 9))
        return sock.getsockname()[0]


def request_json(base, token, path, body=None, timeout=6.0):
    data = None if body is None else json.dumps(body).encode("utf-8")
    request = Request(base + path, data=data, method="GET" if body is None else "POST")
    request.add_header("X-Update-Token", token)
    if data is not None:
        request.add_header("Content-Type", "application/json")
    # A corporate HTTP proxy must not intercept a local ESP32 request.
    opener = build_opener(ProxyHandler({}))
    try:
        with opener.open(request, timeout=timeout) as response:
            raw = response.read(16385)
    except HTTPError as exc:
        raw = exc.read(4096).decode("utf-8", "replace")
        try:
            reason = json.loads(raw).get("error", raw)
        except (ValueError, AttributeError):
            reason = raw
        raise ApiError(exc.code, reason) from exc
    if len(raw) > 16384:
        raise UpdateFailed("ESP32 returned an unexpectedly large response")
    try:
        result = json.loads(raw)
    except ValueError as exc:
        raise UpdateFailed("ESP32 did not return JSON; check its update port") from exc
    if not isinstance(result, dict):
        raise UpdateFailed("Unexpected ESP32 response")
    return result


class FileServer:
    """Serve an immutable temporary snapshot at a random, exact URL path."""

    def __init__(self, snapshot, bind_ip, port):
        self.path = Path(snapshot)
        self.size = self.path.stat().st_size
        self.url_path = "/" + secrets.token_hex(16) + "/display.tft"
        self.started_transfer = threading.Event()
        self.completed_transfer = threading.Event()
        self.httpd = None
        self.thread = None
        owner = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.0"

            def setup(self):
                super().setup()
                # A slow display may take time to ACK a block. Bound abandoned
                # client threads without timing out ordinary serial transfers.
                self.connection.settimeout(30)

            def log_message(self, *_args):
                pass

            def do_HEAD(self):
                self.serve_file(head_only=True)

            def do_GET(self):
                self.serve_file(head_only=False)

            def serve_file(self, head_only):
                if self.path != owner.url_path:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(owner.size))
                self.send_header("Cache-Control", "no-store")
                self.send_header("Connection", "close")
                self.end_headers()
                if head_only:
                    return
                owner.started_transfer.set()
                try:
                    with owner.path.open("rb") as source:
                        shutil.copyfileobj(source, self.wfile, length=65536)
                    self.wfile.flush()
                    owner.completed_transfer.set()
                except (BrokenPipeError, ConnectionResetError, TimeoutError, OSError):
                    # The ESP32 reports the actual transfer/serial outcome.
                    pass

        self.httpd = ThreadingHTTPServer((bind_ip, port), Handler)
        self.httpd.daemon_threads = True
        self.port = self.httpd.server_address[1]

    def __enter__(self):
        self.thread = threading.Thread(target=self.httpd.serve_forever,
                                       kwargs={"poll_interval": 0.1}, daemon=True)
        self.thread.start()
        return self

    def __exit__(self, *_args):
        self.httpd.shutdown()
        self.httpd.server_close()
        self.thread.join(timeout=2)


def wait_for_result(base, token, job, deadline, accepted=None, poll_seconds=1.0):
    """Keep the file server alive across transient status-request failures."""
    status = accepted
    last_display = None
    outage_printed = False
    while True:
        if status is not None:
            if status.get("job_id") != job:
                raise UpdateFailed("ESP32 job identity changed (possibly a reboot); update not confirmed")
            state = status.get("status")
            size = int(status.get("size", 0))
            acknowledged = int(status.get("acknowledged", 0))
            phase = status.get("phase", "unknown")
            percent = 100.0 * acknowledged / size if size else 0.0
            display = (phase, int(percent), state)
            if display != last_display:
                print(f"{percent:6.1f}%  {phase}  ({acknowledged:,}/{size:,} bytes acknowledged)",
                      flush=True)
                last_display = display
            if state in TERMINAL_STATES:
                message = status.get("message", state)
                if state == "success" and status.get("display_ready") and acknowledged == size and size > 0:
                    print(f"Success: {message}", flush=True)
                    return status
                raise UpdateFailed(f"{state}: {message}")
            if state != "running":
                raise UpdateFailed(f"Unexpected update state: {state!r}")
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise UpdateFailed("Client deadline expired; upload result is unconfirmed. "
                               "The ESP32 has its own timeout. Query --status before retrying.")
        time.sleep(min(poll_seconds, remaining))
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                status = None
                continue
            status = request_json(base, token, "/api/nextion/status", timeout=min(6, remaining))
            if outage_printed:
                print("ESP32 status connection restored.", flush=True)
            outage_printed = False
        except (URLError, TimeoutError, OSError) as exc:
            status = None
            if not outage_printed:
                print(f"Waiting for ESP32 status connection: {exc}", flush=True)
                outage_printed = True


def run_upload(args, token, base, device_ip):
    source = args.tft.expanduser().resolve(strict=True)
    if not source.is_file() or source.suffix.lower() != ".tft":
        raise ValueError("Choose a regular .tft file exported from Nextion Editor")
    size = source.stat().st_size
    if not 0 < size <= MAX_SIZE:
        raise ValueError("The TFT must be between 1 byte and 128 MiB")
    server_ip = args.serve_ip or source_ip_for(device_ip)
    if ipaddress.IPv4Address(server_ip).is_unspecified:
        raise ValueError("--serve-ip must be an address the ESP32 can reach, not 0.0.0.0")
    bind_ip = args.bind or server_ip
    timeout = args.timeout or max(180, math.ceil(size * 10 / BAUD * 2 + 120))
    if not 30 <= timeout <= MAX_TIMEOUT:
        raise ValueError("Timeout must be between 30 and 21600 seconds; use --timeout explicitly")
    initial = request_json(base, token, "/api/nextion/status")
    if initial.get("busy"):
        raise UpdateFailed("An update is already running. Use --status to inspect it.")
    job = secrets.token_hex(16)
    with tempfile.TemporaryDirectory(prefix="nextion-tft-") as temp:
        snapshot = Path(temp) / "display.tft"
        shutil.copyfile(source, snapshot)
        # The snapshot stays unchanged even if the editor rebuilds the source.
        if snapshot.stat().st_size != size:
            raise UpdateFailed("TFT size changed while copying; export it again and retry")
        with FileServer(snapshot, bind_ip, args.port) as server:
            url = f"http://{server_ip}:{server.port}{server.url_path}"
            print(f"Serving {source.name} ({size:,} bytes) at {server_ip}:{server.port}", flush=True)
            print(f"ESP32: {base}; job: {job}; transfer deadline: {timeout} s", flush=True)
            print("Keep this process running until it reports the result.", flush=True)
            payload = {"job_id": job, "url": url, "size": size, "timeout_s": timeout}
            accepted = None
            # Identical request retries are idempotent in the firmware. In
            # particular, a lost HTTP 202 must not start another flash.
            for attempt in range(3):
                try:
                    accepted = request_json(base, token, "/api/nextion/update", payload)
                    break
                except (URLError, TimeoutError, OSError) as exc:
                    print(f"Trigger response unavailable ({attempt + 1}/3): {exc}", flush=True)
            if accepted is None:
                print("Trigger result uncertain; checking the same job while keeping the file available.",
                      flush=True)
            # Extra time covers bounded display reboot/recovery and a last poll.
            deadline = time.monotonic() + timeout + 90
            try:
                return wait_for_result(base, token, job, deadline, accepted)
            except UpdateFailed:
                if not server.started_transfer.is_set():
                    print("The ESP32 never fetched the TFT. Check --serve-ip, the PC firewall, "
                          "and Wi-Fi client isolation.", file=sys.stderr)
                raise


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tft", type=Path, nargs="?", help="compiled Nextion .tft file")
    parser.add_argument("--device", required=True, help="ESP32 IP/hostname; update port defaults to 8080")
    parser.add_argument("--token", help="shared TFT_UPDATE_TOKEN (or NEXTION_UPDATE_TOKEN environment variable)")
    parser.add_argument("--serve-ip", help="this PC's LAN IPv4 address, advertised to the ESP32")
    parser.add_argument("--bind", help="local bind address (default: --serve-ip or detected LAN address)")
    parser.add_argument("--port", type=int, default=8000, help="PC file-server port (default 8000; 0 chooses a free port)")
    parser.add_argument("--timeout", type=int, help="ESP32 total transfer timeout in seconds (auto-calculated by file size)")
    parser.add_argument("--status", action="store_true", help="show the ESP32's last/current result without starting an update")
    args = parser.parse_args(argv)
    if not args.status and args.tft is None:
        parser.error("provide a .tft file, or use --status")
    if not 0 <= args.port <= 65535:
        parser.error("--port must be between 0 and 65535")
    if args.timeout is not None and not 30 <= args.timeout <= MAX_TIMEOUT:
        parser.error("--timeout must be between 30 and 21600 seconds")
    return args


def main(argv=None):
    args = parse_args(argv)
    try:
        token = args.token or os.environ.get("NEXTION_UPDATE_TOKEN") or getpass.getpass("TFT update token: ")
        if not token:
            raise ValueError("An update token is required")
        device_ip, port = device_address(args.device)
        base = f"http://{device_ip}:{port}"
        if args.status:
            print(json.dumps(request_json(base, token, "/api/nextion/status"), indent=2))
        else:
            run_upload(args, token, base, device_ip)
        return 0
    except KeyboardInterrupt:
        print("\nFile server stopped. An in-progress upload will fail/time out on the ESP32. "
              "Use --status to inspect its result.", file=sys.stderr)
        return 130
    except (ApiError, UpdateFailed, ValueError, OSError, URLError, EOFError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
