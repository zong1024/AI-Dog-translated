#include <Arduino.h>

namespace {

constexpr uint32_t BAUD = 115200;
constexpr int UART0_RX_PIN = 20;
constexpr int UART0_TX_PIN = 21;

}  // namespace

void setup() {
  Serial.begin(BAUD);
  Serial0.begin(BAUD, SERIAL_8N1, UART0_RX_PIN, UART0_TX_PIN);
  delay(800);
}

void loop() {
  char line[80];
  snprintf(line, sizeof(line), "APP_SERIAL_ALIVE millis=%lu", static_cast<unsigned long>(millis()));
  Serial.println(line);
  Serial.flush();
  Serial0.println(line);
  Serial0.flush();
  delay(1000);
}
