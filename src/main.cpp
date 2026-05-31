#include <Arduino.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <TinyGPSPlus.h>
#include <driver/i2s.h>

#ifndef ENABLE_AUDIO_PROMPT
#define ENABLE_AUDIO_PROMPT 0
#endif

#if ENABLE_AUDIO_PROMPT
#include <math.h>
#include "warning_prompt.h"
#endif

namespace {

constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint8_t MAGIC[] = {'D', 'B', 'R', 'K'};
constexpr size_t HEADER_SIZE = 27;
constexpr uint32_t MAX_RESULT_AGE_MS = 10000;
constexpr uint32_t CAPTURE_PERIOD_MS = 5000;
constexpr uint32_t ALERT_DEFAULT_MS = 5000;

constexpr int DTU_RX_PIN = 0;  // ESP32-C3 receives from Air780EP TXD.
constexpr int DTU_TX_PIN = 1;  // ESP32-C3 transmits to Air780EP RXD.
constexpr int GPS_RX_PIN = 3;  // ESP32-C3 receives ATGM336H NMEA TXD.

constexpr int I2S_BCLK_PIN = 4;
constexpr int I2S_LRCLK_PIN = 5;
constexpr int I2S_DIN_PIN = 6;   // INMP441 SD.
#if ENABLE_AUDIO_PROMPT
constexpr int I2S_DOUT_PIN = 7;  // Optional MAX98357A DIN.
#endif

constexpr int LED_DATA_PIN = 10;
constexpr int LED_COUNT = 8;

constexpr int BATTERY_ADC_PIN = -1;  // Set to an ADC GPIO after adding a voltage divider.
constexpr float BATTERY_DIVIDER_RATIO = 2.0f;
constexpr float ADC_REFERENCE_MV = 3300.0f;

constexpr uint32_t SAMPLE_RATE = 16000;
constexpr uint32_t RECORD_MS = 4000;
constexpr size_t AUDIO_SAMPLES = SAMPLE_RATE * RECORD_MS / 1000;
constexpr size_t WAV_HEADER_SIZE = 44;
constexpr size_t WAV_DATA_SIZE = AUDIO_SAMPLES * sizeof(int16_t);
constexpr size_t WAV_BUFFER_SIZE = WAV_HEADER_SIZE + WAV_DATA_SIZE;

HardwareSerial DtuSerial(1);
HardwareSerial GpsSerial(0);
TinyGPSPlus gps;
CRGB leds[LED_COUNT];

uint8_t *wavBuffer = nullptr;
uint32_t sequenceNumber = 1;
uint32_t lastCaptureStartedMs = 0;

class Crc32 {
public:
  void reset() {
    value_ = 0xFFFFFFFFu;
  }

  void update(const uint8_t *data, size_t len) {
    while (len-- > 0) {
      value_ ^= *data++;
      for (uint8_t i = 0; i < 8; ++i) {
        const uint32_t mask = 0u - (value_ & 1u);
        value_ = (value_ >> 1) ^ (0xEDB88320u & mask);
      }
    }
  }

  uint32_t finalize() const {
    return value_ ^ 0xFFFFFFFFu;
  }

private:
  uint32_t value_ = 0xFFFFFFFFu;
};

void writeLe16(uint8_t *dst, uint16_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFFu);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void writeLe32(uint8_t *dst, uint32_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFFu);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
  dst[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
  dst[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

void writeLeI32(uint8_t *dst, int32_t value) {
  writeLe32(dst, static_cast<uint32_t>(value));
}

void writeWavHeader(uint8_t *buffer, uint32_t sampleRate, uint32_t dataBytes) {
  memcpy(buffer + 0, "RIFF", 4);
  writeLe32(buffer + 4, 36 + dataBytes);
  memcpy(buffer + 8, "WAVE", 4);
  memcpy(buffer + 12, "fmt ", 4);
  writeLe32(buffer + 16, 16);
  writeLe16(buffer + 20, 1);  // PCM.
  writeLe16(buffer + 22, 1);  // mono.
  writeLe32(buffer + 24, sampleRate);
  writeLe32(buffer + 28, sampleRate * sizeof(int16_t));
  writeLe16(buffer + 32, sizeof(int16_t));
  writeLe16(buffer + 34, 16);
  memcpy(buffer + 36, "data", 4);
  writeLe32(buffer + 40, dataBytes);
}

uint16_t readBatteryMv() {
  if (BATTERY_ADC_PIN < 0) {
    return 0;
  }
  const uint16_t raw = analogRead(BATTERY_ADC_PIN);
  const float adcMv = (static_cast<float>(raw) / 4095.0f) * ADC_REFERENCE_MV;
  return static_cast<uint16_t>(adcMv * BATTERY_DIVIDER_RATIO);
}

uint32_t currentTimestampSeconds() {
  if (gps.date.isValid() && gps.time.isValid()) {
    tm t = {};
    t.tm_year = gps.date.year() - 1900;
    t.tm_mon = gps.date.month() - 1;
    t.tm_mday = gps.date.day();
    t.tm_hour = gps.time.hour();
    t.tm_min = gps.time.minute();
    t.tm_sec = gps.time.second();
    return static_cast<uint32_t>(mktime(&t));
  }
  return millis() / 1000;
}

int32_t latE7() {
  if (!gps.location.isValid()) {
    return 0;
  }
  return static_cast<int32_t>(gps.location.lat() * 10000000.0);
}

int32_t lonE7() {
  if (!gps.location.isValid()) {
    return 0;
  }
  return static_cast<int32_t>(gps.location.lng() * 10000000.0);
}

void serviceGps(uint32_t durationMs) {
  const uint32_t start = millis();
  do {
    while (GpsSerial.available() > 0) {
      gps.encode(static_cast<char>(GpsSerial.read()));
    }
    delay(1);
  } while (millis() - start < durationMs);
}

bool initI2s() {
  i2s_config_t config = {};
  uint32_t i2sMode = I2S_MODE_MASTER | I2S_MODE_RX;
#if ENABLE_AUDIO_PROMPT
  i2sMode |= I2S_MODE_TX;
#endif
  config.mode = static_cast<i2s_mode_t>(i2sMode);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 8;
  config.dma_buf_len = 256;
  config.use_apll = false;
  config.tx_desc_auto_clear = ENABLE_AUDIO_PROMPT;
  config.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = I2S_BCLK_PIN;
  pins.ws_io_num = I2S_LRCLK_PIN;
#if ENABLE_AUDIO_PROMPT
  pins.data_out_num = I2S_DOUT_PIN;
#else
  pins.data_out_num = I2S_PIN_NO_CHANGE;
#endif
  pins.data_in_num = I2S_DIN_PIN;

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &config, 0, nullptr);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("I2S install failed: %d\n", err);
    return false;
  }
  err = i2s_set_pin(I2S_NUM_0, &pins);
  if (err != ESP_OK) {
    Serial.printf("I2S pin setup failed: %d\n", err);
    return false;
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
  return true;
}

bool recordWav() {
  if (wavBuffer == nullptr) {
    return false;
  }

  writeWavHeader(wavBuffer, SAMPLE_RATE, WAV_DATA_SIZE);
  i2s_zero_dma_buffer(I2S_NUM_0);

  size_t sampleIndex = 0;
  int32_t i2sSamples[256];
  uint32_t silenceWindow = 0;

  while (sampleIndex < AUDIO_SAMPLES) {
    size_t bytesRead = 0;
    const esp_err_t err = i2s_read(I2S_NUM_0, i2sSamples, sizeof(i2sSamples), &bytesRead, pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
      Serial.printf("I2S read failed: %d\n", err);
      return false;
    }

    const size_t samplesRead = bytesRead / sizeof(int32_t);
    for (size_t i = 0; i < samplesRead && sampleIndex < AUDIO_SAMPLES; ++i) {
      int32_t sample = i2sSamples[i] >> 14;
      sample = constrain(sample, -32768, 32767);
      const int16_t pcm = static_cast<int16_t>(sample);
      writeLe16(wavBuffer + WAV_HEADER_SIZE + sampleIndex * sizeof(int16_t), static_cast<uint16_t>(pcm));
      silenceWindow += abs(pcm);
      ++sampleIndex;
    }
  }

  Serial.printf("Recorded %u bytes, avg amplitude window=%lu\n", static_cast<unsigned>(WAV_BUFFER_SIZE), silenceWindow / AUDIO_SAMPLES);
  return true;
}

void buildHeader(uint8_t *header, uint32_t seq, uint16_t batteryMv) {
  memcpy(header + 0, MAGIC, sizeof(MAGIC));
  header[4] = PROTOCOL_VERSION;
  writeLe32(header + 5, seq);
  writeLe32(header + 9, currentTimestampSeconds());
  writeLeI32(header + 13, latE7());
  writeLeI32(header + 17, lonE7());
  writeLe16(header + 21, batteryMv);
  writeLe32(header + 23, WAV_BUFFER_SIZE);
}

bool sendFrame(uint32_t seq) {
  uint8_t header[HEADER_SIZE];
  buildHeader(header, seq, readBatteryMv());

  Crc32 crc;
  crc.reset();
  crc.update(header, HEADER_SIZE);
  crc.update(wavBuffer, WAV_BUFFER_SIZE);
  const uint32_t finalCrc = crc.finalize();
  uint8_t crcBytes[4];
  writeLe32(crcBytes, finalCrc);

  DtuSerial.write(header, HEADER_SIZE);
  DtuSerial.write(wavBuffer, WAV_BUFFER_SIZE);
  DtuSerial.write(crcBytes, sizeof(crcBytes));
  DtuSerial.flush();

  Serial.printf("Sent seq=%lu wav=%u crc=0x%08lx\n", seq, static_cast<unsigned>(WAV_BUFFER_SIZE), finalCrc);
  return true;
}

void setAllLeds(const CRGB &color) {
  fill_solid(leds, LED_COUNT, color);
  FastLED.show();
}

#if ENABLE_AUDIO_PROMPT
void writeI2sTone(float frequency, uint32_t durationMs, float volume) {
  const size_t totalSamples = SAMPLE_RATE * durationMs / 1000;
  int32_t txBuffer[128];
  size_t writtenSamples = 0;

  while (writtenSamples < totalSamples) {
    const size_t count = min(static_cast<size_t>(128), totalSamples - writtenSamples);
    for (size_t i = 0; i < count; ++i) {
      const float t = static_cast<float>(writtenSamples + i) / static_cast<float>(SAMPLE_RATE);
      const float envelope = (i < 16) ? (static_cast<float>(i) / 16.0f) : 1.0f;
      const int16_t sample = static_cast<int16_t>(sinf(2.0f * PI * frequency * t) * 28000.0f * volume * envelope);
      txBuffer[i] = static_cast<int32_t>(sample) << 16;
    }
    size_t bytesWritten = 0;
    i2s_write(I2S_NUM_0, txBuffer, count * sizeof(int32_t), &bytesWritten, pdMS_TO_TICKS(500));
    writtenSamples += count;
  }
}

void writePcmPrompt(const int16_t *samples, size_t sampleCount) {
  int32_t txBuffer[128];
  size_t offset = 0;
  while (offset < sampleCount) {
    const size_t count = min(static_cast<size_t>(128), sampleCount - offset);
    for (size_t i = 0; i < count; ++i) {
      txBuffer[i] = static_cast<int32_t>(samples[offset + i]) << 16;
    }
    size_t bytesWritten = 0;
    i2s_write(I2S_NUM_0, txBuffer, count * sizeof(int32_t), &bytesWritten, pdMS_TO_TICKS(500));
    offset += count;
  }
}

void playWarningPrompt() {
  if (WARNING_PROMPT_SAMPLE_COUNT > 0 && WARNING_PROMPT_SAMPLE_RATE == SAMPLE_RATE) {
    writePcmPrompt(WARNING_PROMPT_PCM, WARNING_PROMPT_SAMPLE_COUNT);
    return;
  }

  writeI2sTone(880.0f, 240, 0.35f);
  writeI2sTone(0.0f, 80, 0.0f);
  writeI2sTone(1175.0f, 240, 0.35f);
  writeI2sTone(0.0f, 80, 0.0f);
  writeI2sTone(880.0f, 360, 0.35f);
}
#endif

void runAngryAlert(uint32_t alertMs) {
  const uint32_t endAt = millis() + max<uint32_t>(alertMs, 1000);
  uint32_t lastBlink = 0;
  bool on = false;
#if ENABLE_AUDIO_PROMPT
  bool promptPlayed = false;
#endif

  while (static_cast<int32_t>(endAt - millis()) > 0) {
#if ENABLE_AUDIO_PROMPT
    if (!promptPlayed) {
      promptPlayed = true;
      playWarningPrompt();
    }
#endif
    if (millis() - lastBlink >= 180) {
      lastBlink = millis();
      on = !on;
      setAllLeds(on ? CRGB::Red : CRGB::Black);
    }
    serviceGps(5);
  }
  setAllLeds(CRGB::Black);
}

bool readResultLine(String &line, uint32_t timeoutMs) {
  const uint32_t start = millis();
  line = "";
  while (millis() - start < timeoutMs) {
    while (DtuSerial.available() > 0) {
      const char ch = static_cast<char>(DtuSerial.read());
      if (ch == '\n') {
        line.trim();
        return line.length() > 0;
      }
      if (ch != '\r' && line.length() < 256) {
        line += ch;
      }
    }
    serviceGps(2);
  }
  return false;
}

void handleServerResult(uint32_t seq, uint32_t sentAtMs) {
  String line;
  if (!readResultLine(line, MAX_RESULT_AGE_MS)) {
    Serial.println("No server result; defaulting to non_angry");
    return;
  }
  if (millis() - sentAtMs > MAX_RESULT_AGE_MS) {
    Serial.printf("Late result ignored: %s\n", line.c_str());
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, line);
  if (err) {
    Serial.printf("Bad JSON result: %s\n", line.c_str());
    return;
  }

  const uint32_t resultSeq = doc["seq"] | 0;
  if (resultSeq != seq) {
    Serial.printf("Sequence mismatch: expected %lu got %lu\n", seq, resultSeq);
    return;
  }

  const char *label = doc["label"] | "non_angry";
  const uint32_t alertMs = doc["alert_ms"] | ALERT_DEFAULT_MS;
  const float confidence = doc["confidence"] | 0.0f;
  Serial.printf("Result seq=%lu label=%s confidence=%.3f\n", seq, label, confidence);

  if (strcmp(label, "angry") == 0) {
    runAngryAlert(alertMs);
  } else {
    setAllLeds(CRGB::Black);
  }
}

void appSetup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("Dog bark translator collar booting");

  DtuSerial.begin(115200, SERIAL_8N1, DTU_RX_PIN, DTU_TX_PIN);
  GpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, -1);

  FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds, LED_COUNT);
  FastLED.setBrightness(64);
  setAllLeds(CRGB::Black);

  wavBuffer = static_cast<uint8_t *>(malloc(WAV_BUFFER_SIZE));
  if (wavBuffer == nullptr) {
    Serial.printf("Failed to allocate WAV buffer: %u bytes\n", static_cast<unsigned>(WAV_BUFFER_SIZE));
    setAllLeds(CRGB::Orange);
    return;
  }

  if (!initI2s()) {
    setAllLeds(CRGB::Orange);
    return;
  }

  Serial.println("Ready");
}

void appLoop() {
  serviceGps(20);
  if (wavBuffer == nullptr) {
    delay(1000);
    return;
  }

  if (millis() - lastCaptureStartedMs < CAPTURE_PERIOD_MS) {
    delay(10);
    return;
  }
  lastCaptureStartedMs = millis();

  const uint32_t seq = sequenceNumber++;
  Serial.printf("Capture seq=%lu\n", seq);
  setAllLeds(CRGB::Blue);

  if (!recordWav()) {
    setAllLeds(CRGB::Orange);
    delay(500);
    setAllLeds(CRGB::Black);
    return;
  }

  setAllLeds(CRGB::Green);
  const uint32_t sentAtMs = millis();
  sendFrame(seq);
  setAllLeds(CRGB::Black);
  handleServerResult(seq, sentAtMs);
}

}  // namespace

void setup() {
  appSetup();
}

void loop() {
  appLoop();
}
