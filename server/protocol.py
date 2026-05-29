from __future__ import annotations

import asyncio
import dataclasses
import struct
import zlib


MAGIC = b"DBRK"
VERSION = 1
HEADER_STRUCT = struct.Struct("<4sBIIiiHI")
CRC_STRUCT = struct.Struct("<I")
MAX_WAV_BYTES = 512 * 1024


class ProtocolError(ValueError):
    pass


@dataclasses.dataclass(frozen=True)
class DogBarkFrame:
    version: int
    seq: int
    timestamp: int
    lat_e7: int
    lon_e7: int
    battery_mv: int
    wav_bytes: bytes

    @property
    def latitude(self) -> float | None:
        return self.lat_e7 / 10_000_000 if self.lat_e7 else None

    @property
    def longitude(self) -> float | None:
        return self.lon_e7 / 10_000_000 if self.lon_e7 else None


def build_frame(
    *,
    seq: int,
    timestamp: int,
    lat_e7: int,
    lon_e7: int,
    battery_mv: int,
    wav_bytes: bytes,
    version: int = VERSION,
) -> bytes:
    if len(wav_bytes) > MAX_WAV_BYTES:
        raise ProtocolError(f"WAV payload too large: {len(wav_bytes)}")
    header = HEADER_STRUCT.pack(MAGIC, version, seq, timestamp, lat_e7, lon_e7, battery_mv, len(wav_bytes))
    crc = zlib.crc32(header)
    crc = zlib.crc32(wav_bytes, crc) & 0xFFFFFFFF
    return header + wav_bytes + CRC_STRUCT.pack(crc)


def parse_frame(data: bytes) -> DogBarkFrame:
    if len(data) < HEADER_STRUCT.size + CRC_STRUCT.size:
        raise ProtocolError("frame too short")

    header = data[: HEADER_STRUCT.size]
    magic, version, seq, timestamp, lat_e7, lon_e7, battery_mv, wav_len = HEADER_STRUCT.unpack(header)
    if magic != MAGIC:
        raise ProtocolError(f"bad magic: {magic!r}")
    if version != VERSION:
        raise ProtocolError(f"unsupported version: {version}")
    if wav_len > MAX_WAV_BYTES:
        raise ProtocolError(f"WAV payload too large: {wav_len}")

    expected_size = HEADER_STRUCT.size + wav_len + CRC_STRUCT.size
    if len(data) != expected_size:
        raise ProtocolError(f"bad frame length: got {len(data)} expected {expected_size}")

    wav_bytes = data[HEADER_STRUCT.size : HEADER_STRUCT.size + wav_len]
    (received_crc,) = CRC_STRUCT.unpack(data[-CRC_STRUCT.size :])
    crc = zlib.crc32(header)
    crc = zlib.crc32(wav_bytes, crc) & 0xFFFFFFFF
    if crc != received_crc:
        raise ProtocolError(f"crc mismatch: got 0x{received_crc:08x} expected 0x{crc:08x}")

    return DogBarkFrame(
        version=version,
        seq=seq,
        timestamp=timestamp,
        lat_e7=lat_e7,
        lon_e7=lon_e7,
        battery_mv=battery_mv,
        wav_bytes=wav_bytes,
    )


async def read_frame(reader: asyncio.StreamReader) -> DogBarkFrame:
    header = await reader.readexactly(HEADER_STRUCT.size)
    magic, version, seq, timestamp, lat_e7, lon_e7, battery_mv, wav_len = HEADER_STRUCT.unpack(header)
    if magic != MAGIC:
        raise ProtocolError(f"bad magic: {magic!r}")
    if version != VERSION:
        raise ProtocolError(f"unsupported version: {version}")
    if wav_len > MAX_WAV_BYTES:
        raise ProtocolError(f"WAV payload too large: {wav_len}")

    wav_bytes = await reader.readexactly(wav_len)
    crc_bytes = await reader.readexactly(CRC_STRUCT.size)
    return parse_frame(header + wav_bytes + crc_bytes)
