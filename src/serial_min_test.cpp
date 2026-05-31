#include <Arduino.h>

namespace {

#ifndef DEBUG_BAUD
#define DEBUG_BAUD 115200
#endif

#ifndef BITBANG_DEBUG_TX
#define BITBANG_DEBUG_TX 0
#endif

constexpr uint32_t BAUD = DEBUG_BAUD;

#ifndef DEBUG_UART_RX_PIN
#define DEBUG_UART_RX_PIN 20
#endif

#ifndef DEBUG_UART_TX_PIN
#define DEBUG_UART_TX_PIN 21
#endif

#ifndef DEBUG_UART_NUM
#define DEBUG_UART_NUM 0
#endif

constexpr int UART0_RX_PIN = DEBUG_UART_RX_PIN;
constexpr int UART0_TX_PIN = DEBUG_UART_TX_PIN;

HardwareSerial DebugSerial(DEBUG_UART_NUM);

void bitbangWriteByte(uint8_t value) {
#if BITBANG_DEBUG_TX
  const uint32_t bitUs = 1000000UL / BAUD;
  digitalWrite(UART0_TX_PIN, LOW);
  delayMicroseconds(bitUs);
  for (uint8_t i = 0; i < 8; ++i) {
    digitalWrite(UART0_TX_PIN, (value >> i) & 0x01);
    delayMicroseconds(bitUs);
  }
  digitalWrite(UART0_TX_PIN, HIGH);
  delayMicroseconds(bitUs);
#else
  (void)value;
#endif
}

void bitbangPrintln(const char *line) {
#if BITBANG_DEBUG_TX
  for (const char *p = line; *p != '\0'; ++p) {
    bitbangWriteByte(static_cast<uint8_t>(*p));
  }
  bitbangWriteByte('\r');
  bitbangWriteByte('\n');
#else
  (void)line;
#endif
}

}  // namespace

void setup() {
  Serial.begin(BAUD);
#if BITBANG_DEBUG_TX
  pinMode(UART0_TX_PIN, OUTPUT);
  digitalWrite(UART0_TX_PIN, HIGH);
#else
  DebugSerial.begin(BAUD, SERIAL_8N1, UART0_RX_PIN, UART0_TX_PIN);
#endif
  delay(800);
#if BITBANG_DEBUG_TX
  char ready[96];
  snprintf(ready,
           sizeof(ready),
           "SERIAL_MIN_READY bitbang=1 baud=%lu tx=%d",
           static_cast<unsigned long>(BAUD),
           UART0_TX_PIN);
  bitbangPrintln(ready);
#else
  DebugSerial.printf("SERIAL_MIN_READY uart=%d baud=%lu rx=%d tx=%d\n",
                     DEBUG_UART_NUM,
                     static_cast<unsigned long>(BAUD),
                     UART0_RX_PIN,
                     UART0_TX_PIN);
#endif
}

void loop() {
  char line[80];
  snprintf(line, sizeof(line), "APP_SERIAL_ALIVE millis=%lu", static_cast<unsigned long>(millis()));
  Serial.println(line);
  Serial.flush();
#if !BITBANG_DEBUG_TX
  DebugSerial.println(line);
  DebugSerial.flush();
#endif
  bitbangPrintln(line);
  delay(1000);
}
