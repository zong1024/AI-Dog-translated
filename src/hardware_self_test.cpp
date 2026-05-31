#include <Arduino.h>
#include <FastLED.h>
#include <TinyGPSPlus.h>
#include <driver/i2s.h>

namespace {

constexpr uint32_t DEBUG_BAUD = 115200;
constexpr int DEBUG_RX_PIN = 20;
constexpr int DEBUG_TX_PIN = 21;

constexpr uint32_t GPS_BAUD = 9600;
constexpr int GPS_RX_PIN = 3;

constexpr int I2S_BCLK_PIN = 4;
constexpr int I2S_LRCLK_PIN = 5;
constexpr int I2S_DIN_PIN = 6;

constexpr int LED_DATA_PIN = 10;
constexpr int LED_COUNT = 8;

constexpr uint32_t SAMPLE_RATE = 16000;
constexpr uint32_t GPS_TEST_MS = 12000;
constexpr uint32_t MIC_TEST_MS = 2000;
constexpr uint32_t MIC_MIN_AVG_AMPLITUDE = 20;

HardwareSerial GpsSerial(1);
TinyGPSPlus gps;
CRGB leds[LED_COUNT];

uint32_t gpsChars = 0;
uint32_t gpsSentences = 0;
uint32_t micAvgAmplitude = 0;
bool micOk = false;
bool gpsDataOk = false;
bool gpsFixOk = false;

void logLine(const char *message) {
  Serial.println(message);
  Serial.flush();
}

void setLed(int index, const CRGB &color) {
  if (index >= 0 && index < LED_COUNT) {
    leds[index] = color;
    FastLED.show();
  }
}

void setAll(const CRGB &color) {
  fill_solid(leds, LED_COUNT, color);
  FastLED.show();
}

void bootAnimation() {
  setAll(CRGB::Black);
  for (int round = 0; round < 2; ++round) {
    for (int i = 0; i < LED_COUNT; ++i) {
      setAll(CRGB::Black);
      setLed(i, CRGB::White);
      delay(80);
    }
  }
  setAll(CRGB::Black);
}

bool initI2sRx() {
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 6;
  config.dma_buf_len = 256;
  config.use_apll = false;
  config.tx_desc_auto_clear = false;
  config.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = I2S_BCLK_PIN;
  pins.ws_io_num = I2S_LRCLK_PIN;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
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

void testGps() {
  logLine("GPS test start");
  setAll(CRGB::Cyan);
  const uint32_t start = millis();
  while (millis() - start < GPS_TEST_MS) {
    while (GpsSerial.available() > 0) {
      gps.encode(static_cast<char>(GpsSerial.read()));
    }
    delay(2);
  }

  gpsChars = gps.charsProcessed();
  gpsSentences = gps.sentencesWithFix();
  gpsDataOk = gpsChars > 32;
  gpsFixOk = gps.location.isValid();

  Serial.printf("GPS chars=%lu sentences_with_fix=%lu fix=%s sat_valid=%s satellites=%lu\n",
                static_cast<unsigned long>(gpsChars),
                static_cast<unsigned long>(gpsSentences),
                gpsFixOk ? "YES" : "NO",
                gps.satellites.isValid() ? "YES" : "NO",
                gps.satellites.isValid() ? static_cast<unsigned long>(gps.satellites.value()) : 0UL);
}

void testMic() {
  logLine("Microphone test start");
  setAll(CRGB::Blue);
  if (!initI2sRx()) {
    micOk = false;
    return;
  }

  int32_t samples[256];
  uint64_t amplitudeSum = 0;
  uint32_t sampleCount = 0;
  const uint32_t start = millis();
  while (millis() - start < MIC_TEST_MS) {
    size_t bytesRead = 0;
    const esp_err_t err = i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytesRead, pdMS_TO_TICKS(500));
    if (err != ESP_OK) {
      Serial.printf("I2S read failed: %d\n", err);
      continue;
    }

    const size_t count = bytesRead / sizeof(int32_t);
    for (size_t i = 0; i < count; ++i) {
      const int32_t pcm = samples[i] >> 14;
      amplitudeSum += abs(pcm);
      ++sampleCount;
    }
  }

  micAvgAmplitude = sampleCount == 0 ? 0 : static_cast<uint32_t>(amplitudeSum / sampleCount);
  micOk = micAvgAmplitude >= MIC_MIN_AVG_AMPLITUDE;
  Serial.printf("MIC samples=%lu avg_amplitude=%lu ok=%s\n",
                static_cast<unsigned long>(sampleCount),
                static_cast<unsigned long>(micAvgAmplitude),
                micOk ? "YES" : "NO");
}

void showFinalResult() {
  setAll(CRGB::Black);
  setLed(0, gpsFixOk ? CRGB::Green : (gpsDataOk ? CRGB::Yellow : CRGB::Red));
  setLed(1, micOk ? CRGB::Green : CRGB::Red);
  setLed(2, CRGB::Blue);
}

void printLegend() {
  Serial.println();
  Serial.println("Hardware self-test LED legend:");
  Serial.println("LED0 GPS: green=fix, yellow=NMEA/no fix, red=no data");
  Serial.println("LED1 MIC: green=audio amplitude detected, red=no signal");
  Serial.println("LED2 RUN: blue=firmware running");
  Serial.println();
}

}  // namespace

void setup() {
  Serial.begin(DEBUG_BAUD, SERIAL_8N1, DEBUG_RX_PIN, DEBUG_TX_PIN);
  delay(1000);
  FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds, LED_COUNT);
  FastLED.setBrightness(48);
  GpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, -1);

  printLegend();
  bootAnimation();
  testGps();
  testMic();
  showFinalResult();
  logLine("Hardware self-test done");
}

void loop() {
  showFinalResult();
  delay(1000);
}
