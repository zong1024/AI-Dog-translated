#pragma once

#include <stddef.h>
#include <stdint.h>

// Optional 16 kHz, 16-bit mono PCM prompt for:
// "请保持距离，狗狗可能紧张".
//
// The firmware falls back to a short alert tone when this array is empty. To use
// a real spoken prompt, generate this header from a WAV file with:
//
//   python3 tools/wav_to_prompt_header.py warning_prompt.wav include/warning_prompt.h
//
constexpr uint32_t WARNING_PROMPT_SAMPLE_RATE = 16000;
constexpr size_t WARNING_PROMPT_SAMPLE_COUNT = 0;
constexpr int16_t WARNING_PROMPT_PCM[1] = {0};
