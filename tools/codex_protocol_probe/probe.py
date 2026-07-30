#!/usr/bin/env python3
"""Inspect fixtures or exchange framed RPC with a compatible HID device.

The optional live commands require the third-party ``hid`` Python module.
Fixture inspection and self-tests use only the standard library.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path
from typing import Any

from .protocol import CHANNEL_RPC, Reassembler, encode_message

DEFAULT_VID = 0x303A
DEFAULT_PID = 0x8297
DEFAULT_USAGE_PAGE = 0xFF00
FIXTURE_DIR = Path(__file__).with_name("fixtures")


def compact_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def load_rpc_fixtures() -> dict[str, Any]:
    return json.loads((FIXTURE_DIR / "rpc_messages.json").read_text())


def select_fixture(name: str) -> dict[str, Any]:
    fixtures = load_rpc_fixtures()
    for group in ("requests", "notifications"):
        for fixture in fixtures[group]:
            if fixture["name"] == name:
                return fixture
    raise KeyError(name)


def import_hid():
    try:
        import hid  # type: ignore
    except ImportError as error:
        raise SystemExit(
            "live HID commands require the 'hid' module (hidapi); "
            "fixture commands do not"
        ) from error
    return hid


def compatible_devices(hid, vid: int, pid: int, usage_page: int):
    return [
        device
        for device in hid.enumerate(vid, pid)
        if device.get("usage_page") == usage_page
    ]


def open_device(hid, vid: int, pid: int, usage_page: int):
    devices = compatible_devices(hid, vid, pid, usage_page)
    if not devices:
        raise SystemExit(
            f"no vendor HID device found at {vid:04x}:{pid:04x} "
            f"usage page {usage_page:#06x}"
        )
    device = hid.device()
    device.open_path(devices[0]["path"])
    device.set_nonblocking(1)
    return device


class RawHidDevice:
    """Minimal Linux hidraw adapter with the same methods used by this probe."""

    def __init__(self, path: str):
        self.path = path
        self.fd = os.open(path, os.O_RDWR | os.O_NONBLOCK)

    def read(self, size: int):
        try:
            return list(os.read(self.fd, size))
        except BlockingIOError:
            return []

    def write(self, data: bytes):
        return os.write(self.fd, data)

    def close(self):
        os.close(self.fd)


def open_live_device(args: argparse.Namespace):
    if args.path:
        return RawHidDevice(args.path)
    hid = import_hid()
    return open_device(hid, args.vid, args.pid, args.usage_page)


def read_one_message(
    device,
    timeout: float,
    expected_id: int | None = None,
    notifications: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    """Read one response, demultiplexing asynchronous device notifications."""
    reassembler = Reassembler()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        data = device.read(64)
        if not data:
            time.sleep(0.01)
            continue
        report = bytes(data)
        # hidapi backends differ on whether reads retain the report ID.
        if len(report) == 63:
            report = bytes((6,)) + report
        for channel, message in reassembler.feed(report):
            if channel == CHANNEL_RPC:
                decoded = json.loads(message)
                if "method" in decoded and "id" not in decoded:
                    if notifications is not None:
                        notifications.append(decoded)
                    continue
                if expected_id is None or decoded.get("id") == expected_id:
                    return decoded
                raise SystemExit(
                    f"unexpected RPC message while waiting for ID "
                    f"{expected_id}: {decoded!r}"
                )
    raise SystemExit(f"timed out after {timeout:.1f}s waiting for RPC response")


def command_list(_args: argparse.Namespace) -> int:
    fixtures = load_rpc_fixtures()
    for group in ("requests", "notifications"):
        for fixture in fixtures[group]:
            print(f"{group[:-1]:12} {fixture['name']}")
    return 0


def command_show(args: argparse.Namespace) -> int:
    try:
        fixture = select_fixture(args.name)
    except KeyError:
        raise SystemExit(f"unknown fixture: {args.name}")
    message = compact_json(fixture["message"])
    print(message)
    for index, report in enumerate(
        encode_message(message, terminate=args.terminate), start=1
    ):
        print(f"{index:02d}: {report.hex()}")
    return 0


def command_devices(args: argparse.Namespace) -> int:
    hid = import_hid()
    devices = compatible_devices(hid, args.vid, args.pid, args.usage_page)
    print(json.dumps(devices, indent=2, default=str))
    return 0 if devices else 1


def command_send(args: argparse.Namespace) -> int:
    message_object: dict[str, Any] | None = None
    if args.fixture:
        try:
            message_object = select_fixture(args.fixture)["message"]
        except KeyError:
            raise SystemExit(f"unknown fixture: {args.fixture}")
        message = compact_json(message_object)
        expects_response = "id" in message_object
        expected_id = message_object.get("id")
    elif args.json:
        try:
            message_object = json.loads(args.json)
        except json.JSONDecodeError as error:
            raise SystemExit(f"invalid --json value: {error}") from error
        message = compact_json(message_object)
        expects_response = "id" in message_object
        expected_id = message_object.get("id")
    else:
        message = args.raw
        expects_response = True
        expected_id = None
    if not isinstance(expected_id, int) or not 0 <= expected_id <= 998:
        expected_id = None

    device = open_live_device(args)
    try:
        for report in encode_message(message):
            written = device.write(report)
            if written not in (len(report), len(report) - 1):
                raise SystemExit(f"short HID write: {written}/{len(report)} bytes")
        if expects_response:
            print(
                json.dumps(
                    read_one_message(device, args.timeout, expected_id),
                    indent=2,
                )
            )
    finally:
        device.close()
    return 0


def write_message(device, message_object: dict[str, Any]) -> None:
    for report in encode_message(compact_json(message_object)):
        written = device.write(report)
        if written not in (len(report), len(report) - 1):
            raise SystemExit(f"short HID write: {written}/{len(report)} bytes")


def command_verify(args: argparse.Namespace) -> int:
    """Run deterministic RPC handshakes and an optional stability loop."""
    device = open_live_device(args)
    started = time.monotonic()
    notifications: list[dict[str, Any]] = []
    try:
        for sequence in range(args.count):
            request_id = sequence % 999
            method = "sys.version" if sequence % 2 == 0 else "device.status"
            write_message(
                device,
                {"method": method, "params": None, "id": request_id},
            )
            response = read_one_message(
                device, args.timeout, request_id, notifications
            )
            if response.get("id") != request_id or "result" not in response:
                raise SystemExit(
                    f"invalid response at exchange {sequence + 1}: {response!r}"
                )
        elapsed = time.monotonic() - started
        print(
            json.dumps(
                {
                    "exchanges": args.count,
                    "elapsed_seconds": round(elapsed, 3),
                    "failures": 0,
                    "notifications": len(notifications),
                },
                indent=2,
            )
        )
    finally:
        device.close()
    return 0


def command_listen(args: argparse.Namespace) -> int:
    """Print device notifications for interactive key/encoder validation."""
    device = open_live_device(args)
    reassembler = Reassembler()
    deadline = time.monotonic() + args.seconds
    try:
        while time.monotonic() < deadline:
            data = device.read(64)
            if not data:
                time.sleep(0.01)
                continue
            report = bytes(data)
            if len(report) == 63:
                report = bytes((6,)) + report
            for channel, message in reassembler.feed(report):
                print(
                    json.dumps(
                        {
                            "channel": channel,
                            "message": json.loads(message),
                        },
                        ensure_ascii=False,
                    ),
                    flush=True,
                )
    finally:
        device.close()
    return 0


def command_soak(args: argparse.Namespace) -> int:
    """Run periodic status RPCs while checking firmware error counters."""
    device = open_live_device(args)
    started = time.monotonic()
    deadline = started + args.hours * 3600.0
    next_progress = started
    exchanges = 0
    baseline: dict[str, int] | None = None
    latest: dict[str, int] = {}
    try:
        while time.monotonic() < deadline:
            request_id = exchanges % 999
            write_message(
                device,
                {"method": "device.status", "params": None, "id": request_id},
            )
            response = read_one_message(device, args.timeout, request_id)
            result = response.get("result", {})
            latest = result.get("diagnostics", {})
            if response.get("id") != request_id or not isinstance(latest, dict):
                raise SystemExit(f"invalid soak response: {response!r}")
            if baseline is None:
                baseline = dict(latest)
            for key in ("rx_dropped", "rx_timeouts", "tx_failed"):
                if latest.get(key, 0) != baseline.get(key, 0):
                    raise SystemExit(
                        f"diagnostic counter changed: {key} "
                        f"{baseline.get(key)} -> {latest.get(key)}"
                    )
            exchanges += 1
            now = time.monotonic()
            if now >= next_progress:
                print(
                    json.dumps(
                        {
                            "elapsed_seconds": round(now - started, 1),
                            "exchanges": exchanges,
                            "diagnostics": latest,
                        }
                    ),
                    flush=True,
                )
                next_progress = now + args.progress_seconds
            time.sleep(max(0.0, args.interval))
    finally:
        device.close()
    print(
        json.dumps(
            {
                "hours": round((time.monotonic() - started) / 3600.0, 3),
                "exchanges": exchanges,
                "diagnostics": latest,
                "failures": 0,
            },
            indent=2,
        )
    )
    return 0


def int_auto(value: str) -> int:
    return int(value, 0)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list", help="list golden RPC fixtures")
    list_parser.set_defaults(func=command_list)

    show_parser = subparsers.add_parser("show", help="show fixture JSON and frames")
    show_parser.add_argument("name")
    show_parser.add_argument(
        "--terminate",
        action="store_true",
        help="append LF as required for device-to-host messages",
    )
    show_parser.set_defaults(func=command_show)

    for name, help_text, function in (
        ("devices", "enumerate compatible vendor HID devices", command_devices),
        ("send", "send one RPC request or notification", command_send),
        ("verify", "run RPC handshake/stability exchanges", command_verify),
        ("listen", "print live key and encoder notifications", command_listen),
        ("soak", "run a timed RPC and diagnostic-counter soak", command_soak),
    ):
        live = subparsers.add_parser(name, help=help_text)
        live.add_argument("--vid", type=int_auto, default=DEFAULT_VID)
        live.add_argument("--pid", type=int_auto, default=DEFAULT_PID)
        live.add_argument("--usage-page", type=int_auto, default=DEFAULT_USAGE_PAGE)
        live.add_argument(
            "--path",
            help="open a Linux hidraw path directly instead of using hidapi",
        )
        live.set_defaults(func=function)
        if name == "send":
            source = live.add_mutually_exclusive_group(required=True)
            source.add_argument("--fixture")
            source.add_argument("--json")
            source.add_argument(
                "--raw",
                help="send raw text without validating it as JSON",
            )
            live.add_argument("--timeout", type=float, default=2.0)
        elif name == "verify":
            live.add_argument("--count", type=int, default=1000)
            live.add_argument("--timeout", type=float, default=2.0)
        elif name == "listen":
            live.add_argument("--seconds", type=float, default=60.0)
        elif name == "soak":
            live.add_argument("--hours", type=float, default=4.0)
            live.add_argument("--interval", type=float, default=1.0)
            live.add_argument("--timeout", type=float, default=2.0)
            live.add_argument("--progress-seconds", type=float, default=60.0)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
