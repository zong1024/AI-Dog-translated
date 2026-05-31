# Dog Translator Collar Hardware

## Pin Map

| Module | Module pin | ESP32-C3 / power connection |
|---|---|---|
| Air780EP DTU | VCC/5V | 5V boost output, >= 2A peak |
| Air780EP DTU | GND | common GND |
| Air780EP DTU | TXD | GPIO0, firmware `DTU_RX_PIN` |
| Air780EP DTU | RXD | GPIO1, firmware `DTU_TX_PIN` |
| ATGM336H GPS | VCC | ESP32-C3 3V3 |
| ATGM336H GPS | GND | common GND |
| ATGM336H GPS | TXD | GPIO3, firmware `GPS_RX_PIN` |
| ATGM336H GPS | RXD/PPS | not connected in v1 |
| INMP441 | VDD | ESP32-C3 3V3 |
| INMP441 | GND | common GND |
| INMP441 | SCK | GPIO4, I2S BCLK |
| INMP441 | WS | GPIO5, I2S LRCLK |
| INMP441 | SD | GPIO6, I2S DIN |
| INMP441 | L/R | GND, left channel |
| WS2812 | 5V | 5V boost output |
| WS2812 | GND | common GND |
| WS2812 | DIN | GPIO10 -> 74AHCT125 -> 330 ohm -> DIN |

## Optional Voice Output

The current confirmed hardware list does not include a speaker amplifier, so
the firmware defaults to LED-only public alerts. If a MAX98357A I2S amplifier is
added later, wire it like this and compile with `-D ENABLE_AUDIO_PROMPT=1`.

| Module | Module pin | ESP32-C3 / power connection |
|---|---|---|
| MAX98357A | VIN | 5V boost output |
| MAX98357A | GND | common GND |
| MAX98357A | BCLK | GPIO4, shared I2S BCLK |
| MAX98357A | LRC | GPIO5, shared I2S LRCLK |
| MAX98357A | DIN | GPIO7, I2S DOUT |
| MAX98357A | SD | VIN, always enabled |
| MAX98357A | SPK+/SPK- | 4 ohm or 8 ohm speaker |

## Power

Use a 1S lithium battery through a physical power switch into a 5V boost module
that can handle at least 2A peak current. Feed 5V to Air780EP, ESP32-C3 5V,
and WS2812. Add a 470-1000uF capacitor near the WS2812 strip and
another near the 4G module. All grounds must be common.

Avoid ESP32-C3 strapping pins GPIO2, GPIO8, and GPIO9 for these peripherals.

## Runtime Behavior

The firmware records 4 seconds of 16kHz mono WAV audio every 5 seconds, sends a
`DBRK` protocol frame through the DTU UART, waits up to 10 seconds for a JSON
line response, and blinks red when the server returns `label=angry`.

If optional voice output hardware is added, make a 16 kHz, 16-bit WAV file with
the phrase "请保持距离，狗狗可能紧张", generate the prompt header, and build the
optional audio environment:

```bash
python3 tools/wav_to_prompt_header.py warning_prompt.wav include/warning_prompt.h
pio run -e esp32-c3-audio
```
