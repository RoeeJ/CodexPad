import json
import unittest
from pathlib import Path

from tools.codex_protocol_probe.protocol import (
    CHANNEL_DEBUG,
    CHANNEL_RPC,
    MAX_PAYLOAD_SIZE,
    ProtocolError,
    Reassembler,
    decode_report,
    encode_message,
    encode_report,
)
from tools.codex_protocol_probe.probe import read_one_message


class FakeDevice:
    def __init__(self, reports):
        self.reports = [list(report) for report in reports]

    def read(self, _size):
        return self.reports.pop(0) if self.reports else []


class ReportTests(unittest.TestCase):
    def test_boundary_payload_lengths(self):
        for size in (0, 1, 60, 61):
            with self.subTest(size=size):
                report = encode_report(bytes(range(size)))
                self.assertEqual(len(report), 64)
                channel, payload = decode_report(report)
                self.assertEqual(channel, CHANNEL_RPC)
                self.assertEqual(payload, bytes(range(size)))
                self.assertEqual(report[3 + size :], bytes(61 - size))

    def test_rejects_oversized_payload(self):
        with self.assertRaises(ProtocolError):
            encode_report(bytes(MAX_PAYLOAD_SIZE + 1))

    def test_rejects_bad_report_shape(self):
        for report in (
            bytes(63),
            bytes(65),
            bytes([5, CHANNEL_RPC, 0]) + bytes(61),
            bytes([6, 99, 0]) + bytes(61),
            bytes([6, CHANNEL_RPC, 62]) + bytes(61),
        ):
            with self.subTest(report=report[:3]):
                with self.assertRaises(ProtocolError):
                    decode_report(report)


class MessageTests(unittest.TestCase):
    def test_fragment_boundaries(self):
        for size, expected_reports in (
            (0, 1),
            (1, 1),
            (61, 1),
            (62, 2),
            (122, 2),
            (123, 3),
        ):
            with self.subTest(size=size):
                reports = encode_message(b"x" * size)
                self.assertEqual(len(reports), expected_reports)
                self.assertEqual(
                    b"".join(decode_report(report)[1] for report in reports),
                    b"x" * size,
                )

    def test_utf8_is_reassembled_as_bytes(self):
        message = '{"method":"v.oai.hid","params":{"k":"AG00","label":"שלום"}}'
        reports = encode_message(message, terminate=True)
        completed = Reassembler().feed_all(reports)
        self.assertEqual(completed, [(CHANNEL_RPC, message.encode("utf-8"))])

    def test_channels_are_independent(self):
        reassembler = Reassembler()
        rpc = encode_message("rpc", terminate=True)
        debug = encode_message("debug", CHANNEL_DEBUG, terminate=True)
        completed = reassembler.feed_all((rpc[0], debug[0]))
        self.assertEqual(
            completed,
            [
                (CHANNEL_RPC, b"rpc"),
                (CHANNEL_DEBUG, b"debug"),
            ],
        )

    def test_multiple_lines_in_one_report(self):
        report = encode_report(b"one\r\ntwo\n")
        self.assertEqual(
            Reassembler().feed(report),
            [(CHANNEL_RPC, b"one"), (CHANNEL_RPC, b"two")],
        )

    def test_bounded_reassembly_resets_on_overflow(self):
        reassembler = Reassembler(max_message_size=70)
        reassembler.feed(encode_report(b"x" * 61))
        with self.assertRaises(ProtocolError):
            reassembler.feed(encode_report(b"y" * 10))
        self.assertEqual(
            reassembler.feed(encode_report(b"recovered\n")),
            [(CHANNEL_RPC, b"recovered")],
        )

    def test_response_reader_demultiplexes_notifications(self):
        notification = encode_message(
            '{"method":"v.oai.hid","params":{"k":"ENC_CW","act":2}}',
            terminate=True,
        )
        response = encode_message(
            '{"id":7,"result":{"version":"0.2.4"}}',
            terminate=True,
        )
        seen = []
        decoded = read_one_message(
            FakeDevice(notification + response),
            timeout=0.1,
            expected_id=7,
            notifications=seen,
        )
        self.assertEqual(decoded["id"], 7)
        self.assertEqual(seen[0]["method"], "v.oai.hid")


class FixtureTests(unittest.TestCase):
    fixture_dir = Path(__file__).with_name("fixtures")

    def test_framing_vectors(self):
        fixture = json.loads(
            (self.fixture_dir / "framing_vectors.json").read_text()
        )
        self.assertEqual(fixture["report_id"], 6)
        for vector in fixture["vectors"]:
            with self.subTest(name=vector["name"]):
                payload = bytes.fromhex(vector["payload_hex"])
                report = encode_report(payload, fixture["channel"])
                self.assertTrue(
                    report.hex().startswith(vector["report_prefix_hex"])
                )

    def test_rpc_fixtures_are_compact_json_and_frameable(self):
        fixture = json.loads((self.fixture_dir / "rpc_messages.json").read_text())
        names = set()
        for group in ("requests", "notifications"):
            for entry in fixture[group]:
                self.assertNotIn(entry["name"], names)
                names.add(entry["name"])
                message = json.dumps(
                    entry["message"],
                    ensure_ascii=False,
                    separators=(",", ":"),
                )
                reports = encode_message(message, terminate=True)
                completed = Reassembler().feed_all(reports)
                self.assertEqual(
                    json.loads(completed[0][1]),
                    entry["message"],
                )


if __name__ == "__main__":
    unittest.main()
