import unittest

from server.protocol import ProtocolError, build_frame, parse_frame


class ProtocolTests(unittest.TestCase):
    def test_round_trip(self):
        wav = b"RIFF" + b"\x00" * 16
        data = build_frame(seq=7, timestamp=123, lat_e7=224200000, lon_e7=1140600000, battery_mv=3900, wav_bytes=wav)
        frame = parse_frame(data)
        self.assertEqual(frame.seq, 7)
        self.assertEqual(frame.timestamp, 123)
        self.assertEqual(frame.lat_e7, 224200000)
        self.assertEqual(frame.lon_e7, 1140600000)
        self.assertEqual(frame.battery_mv, 3900)
        self.assertEqual(frame.wav_bytes, wav)

    def test_crc_rejects_mutation(self):
        data = bytearray(build_frame(seq=1, timestamp=1, lat_e7=0, lon_e7=0, battery_mv=0, wav_bytes=b"abc"))
        data[-5] ^= 0xFF
        with self.assertRaises(ProtocolError):
            parse_frame(bytes(data))


if __name__ == "__main__":
    unittest.main()
