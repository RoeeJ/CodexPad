"""Work Louder-compatible HID framing used by the Codex controller probe.

This module is an independent implementation of an observed wire format. It
contains no Work Louder source code.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Iterable

REPORT_ID = 6
REPORT_SIZE = 64
MAX_PAYLOAD_SIZE = 61
CHANNEL_DEBUG = 1
CHANNEL_RPC = 2
VALID_CHANNELS = frozenset((CHANNEL_DEBUG, CHANNEL_RPC))


class ProtocolError(ValueError):
    """Raised when a report violates the bounded wire contract."""


def encode_report(payload: bytes, channel: int = CHANNEL_RPC) -> bytes:
    """Encode one payload fragment into an exact 64-byte HID report."""
    if channel not in VALID_CHANNELS:
        raise ProtocolError(f"unsupported channel: {channel}")
    if len(payload) > MAX_PAYLOAD_SIZE:
        raise ProtocolError(
            f"payload is {len(payload)} bytes; maximum is {MAX_PAYLOAD_SIZE}"
        )

    report = bytearray(REPORT_SIZE)
    report[0] = REPORT_ID
    report[1] = channel
    report[2] = len(payload)
    report[3 : 3 + len(payload)] = payload
    return bytes(report)


def decode_report(report: bytes) -> tuple[int, bytes]:
    """Validate and decode one exact 64-byte HID report."""
    if len(report) != REPORT_SIZE:
        raise ProtocolError(
            f"report is {len(report)} bytes; expected exactly {REPORT_SIZE}"
        )
    if report[0] != REPORT_ID:
        raise ProtocolError(f"unexpected report id: {report[0]}")

    channel = report[1]
    if channel not in VALID_CHANNELS:
        raise ProtocolError(f"unsupported channel: {channel}")

    payload_size = report[2]
    if payload_size > MAX_PAYLOAD_SIZE:
        raise ProtocolError(f"invalid payload length: {payload_size}")
    return channel, bytes(report[3 : 3 + payload_size])


def encode_message(
    message: str | bytes,
    channel: int = CHANNEL_RPC,
    *,
    terminate: bool = False,
) -> list[bytes]:
    """Fragment UTF-8 bytes into reports without splitting the byte stream.

    ``terminate`` adds one LF when the message does not already end in CR/LF.
    Device-to-host JSON should set it; host-to-device requests need not.
    """
    data = message.encode("utf-8") if isinstance(message, str) else bytes(message)
    if terminate and not data.endswith((b"\n", b"\r")):
        data += b"\n"
    if not data:
        return [encode_report(b"", channel)]
    return [
        encode_report(data[offset : offset + MAX_PAYLOAD_SIZE], channel)
        for offset in range(0, len(data), MAX_PAYLOAD_SIZE)
    ]


@dataclass
class Reassembler:
    """Bounded, newline-delimited per-channel message reassembly."""

    max_message_size: int = 4096
    _buffers: dict[int, bytearray] = field(
        default_factory=lambda: {
            CHANNEL_DEBUG: bytearray(),
            CHANNEL_RPC: bytearray(),
        }
    )

    def reset(self, channel: int | None = None) -> None:
        if channel is None:
            for buffer in self._buffers.values():
                buffer.clear()
            return
        if channel not in VALID_CHANNELS:
            raise ProtocolError(f"unsupported channel: {channel}")
        self._buffers[channel].clear()

    def feed(self, report: bytes) -> list[tuple[int, bytes]]:
        """Consume a report and return all newly completed logical messages."""
        channel, payload = decode_report(report)
        buffer = self._buffers[channel]
        if len(buffer) + len(payload) > self.max_message_size:
            buffer.clear()
            raise ProtocolError(
                f"reassembled message exceeds {self.max_message_size} bytes"
            )
        buffer.extend(payload)

        completed: list[tuple[int, bytes]] = []
        while True:
            newline = buffer.find(b"\n")
            if newline < 0:
                break
            message = bytes(buffer[:newline]).rstrip(b"\r")
            del buffer[: newline + 1]
            if message:
                completed.append((channel, message))
        return completed

    def feed_all(self, reports: Iterable[bytes]) -> list[tuple[int, bytes]]:
        completed: list[tuple[int, bytes]] = []
        for report in reports:
            completed.extend(self.feed(report))
        return completed
