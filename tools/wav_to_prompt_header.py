#!/usr/bin/env python3
from __future__ import annotations

import argparse
import audioop
import textwrap
import wave
from pathlib import Path


def read_pcm16_mono_16k(path: Path) -> list[int]:
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        sample_rate = wav.getframerate()
        frames = wav.readframes(wav.getnframes())

    if sample_width != 2:
        raise SystemExit("WAV must be 16-bit PCM")
    if sample_rate != 16000:
        raise SystemExit("WAV must be 16 kHz")
    if channels == 2:
        frames = audioop.tomono(frames, 2, 0.5, 0.5)
    elif channels != 1:
        raise SystemExit("WAV must be mono or stereo")

    return [int.from_bytes(frames[i : i + 2], "little", signed=True) for i in range(0, len(frames), 2)]


def write_header(samples: list[int], output: Path) -> None:
    values = ", ".join(str(sample) for sample in samples)
    wrapped = "\n".join(textwrap.wrap(values, width=110))
    output.write_text(
        "#pragma once\n\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n\n"
        "constexpr uint32_t WARNING_PROMPT_SAMPLE_RATE = 16000;\n"
        f"constexpr size_t WARNING_PROMPT_SAMPLE_COUNT = {len(samples)};\n"
        "constexpr int16_t WARNING_PROMPT_PCM[] = {\n"
        f"{wrapped}\n"
        "};\n",
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert a 16 kHz PCM WAV prompt to a firmware header")
    parser.add_argument("input_wav", type=Path)
    parser.add_argument("output_header", type=Path)
    args = parser.parse_args()
    write_header(read_pcm16_mono_16k(args.input_wav), args.output_header)


if __name__ == "__main__":
    main()
