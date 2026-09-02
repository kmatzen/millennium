#!/usr/bin/env python3
import importlib.util
import pathlib
import unittest

MODULE_PATH = pathlib.Path(__file__).with_name("virtual_mcu.py")
SPEC = importlib.util.spec_from_file_location("virtual_mcu", MODULE_PATH)
MCU = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MCU)


class ProtocolTest(unittest.TestCase):
    def test_known_crc(self):
        self.assertEqual(MCU.crc16(bytes((2, 2, 2, 0, 2, 2))), 0x6A9B)

    def test_round_trip_and_fragmentation(self):
        encoded = MCU.encode(MCU.KEY, 9, b"5")
        decoder = MCU.Decoder()
        self.assertEqual(decoder.feed(encoded[:3]), [])
        self.assertEqual(decoder.feed(encoded[3:]), [(MCU.KEY, 9, b"5")])

    def test_bad_crc_is_rejected_and_resyncs(self):
        bad = bytearray(MCU.encode(MCU.HOOK, 1, b"U"))
        bad[-1] ^= 1
        good = MCU.encode(MCU.COIN, 2, b"8")
        self.assertEqual(MCU.Decoder().feed(b"junk" + bad + good), [(MCU.COIN, 2, b"8")])


if __name__ == "__main__":
    unittest.main()
