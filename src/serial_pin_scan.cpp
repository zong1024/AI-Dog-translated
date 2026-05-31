#include <Arduino.h>

namespace {

constexpr uint32_t DEBUG_BAUD = 115200;
constexpr int CANDIDATE_TX_PINS[] = {
    21, 20, 19, 18, 1, 0, 10, 7, 6, 5, 4, 3, 2, 8, 9,
};

void printBurst(int txPin) {
  Serial.end();
  delay(50);
  Serial.begin(DEBUG_BAUD, SERIAL_8N1, -1, txPin);
  delay(150);

  for (int i = 0; i < 8; ++i) {
    Serial.printf("\nSERIAL_SCAN TX_PIN=%d BAUD=%lu COUNT=%d\n",
                  txPin,
                  static_cast<unsigned long>(DEBUG_BAUD),
                  i);
    Serial.flush();
    delay(80);
  }
}

}  // namespace

void setup() {
  delay(1000);
}

void loop() {
  for (int txPin : CANDIDATE_TX_PINS) {
    printBurst(txPin);
    delay(350);
  }
}
