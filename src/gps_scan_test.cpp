#include <Arduino.h>
#include <TinyGPSPlus.h>

namespace {

constexpr uint32_t USB_BAUD = 115200;
constexpr uint32_t GPS_BAUD = 9600;
constexpr int DEBUG_RX_PIN = 20;  // ESP32-C3 UART0 RX, connected to the USB-serial bridge.
constexpr int DEBUG_TX_PIN = 21;  // ESP32-C3 UART0 TX, connected to the USB-serial bridge.
constexpr int GPS_RX_PIN = 3;   // Connect to ATGM336H TXD.
constexpr int GPS_TX_PIN = -1;  // Optional. Leave unconnected when only reading NMEA.
constexpr uint32_t STATUS_PERIOD_MS = 1000;
constexpr uint32_t NO_DATA_WARN_MS = 5000;

HardwareSerial GpsSerial(1);
TinyGPSPlus gps;

uint32_t lastStatusMs = 0;
uint32_t lastByteMs = 0;
uint32_t lastChars = 0;

void printStatus() {
  Serial.println();
  Serial.println("========== GPS STATUS ==========");
  Serial.printf("chars=%lu sentences=%lu failed_checksum=%lu\n",
                static_cast<unsigned long>(gps.charsProcessed()),
                static_cast<unsigned long>(gps.sentencesWithFix()),
                static_cast<unsigned long>(gps.failedChecksum()));

  if (gps.satellites.isValid()) {
    Serial.printf("satellites=%lu\n", static_cast<unsigned long>(gps.satellites.value()));
  } else {
    Serial.println("satellites=unknown");
  }

  if (gps.hdop.isValid()) {
    Serial.printf("hdop=%.2f\n", gps.hdop.hdop());
  } else {
    Serial.println("hdop=unknown");
  }

  if (gps.location.isValid()) {
    Serial.printf("fix=YES lat=%.6f lon=%.6f age=%lu ms\n",
                  gps.location.lat(),
                  gps.location.lng(),
                  static_cast<unsigned long>(gps.location.age()));
  } else {
    Serial.println("fix=NO");
  }

  if (gps.altitude.isValid()) {
    Serial.printf("altitude=%.2f m\n", gps.altitude.meters());
  } else {
    Serial.println("altitude=unknown");
  }

  if (gps.speed.isValid()) {
    Serial.printf("speed=%.2f km/h\n", gps.speed.kmph());
  } else {
    Serial.println("speed=unknown");
  }

  if (gps.date.isValid() && gps.time.isValid()) {
    Serial.printf("utc=%04d-%02d-%02d %02d:%02d:%02d\n",
                  gps.date.year(),
                  gps.date.month(),
                  gps.date.day(),
                  gps.time.hour(),
                  gps.time.minute(),
                  gps.time.second());
  } else {
    Serial.println("utc=unknown");
  }

  if (gps.location.isValid()) {
    Serial.println("result=GPS fixed, module is working.");
  } else if (gps.satellites.isValid() && gps.satellites.value() > 0) {
    Serial.println("result=Seeing satellites, still waiting for a position fix.");
  } else {
    Serial.println("result=Still searching satellites.");
  }
}

}  // namespace

void setup() {
  Serial.begin(USB_BAUD, SERIAL_8N1, DEBUG_RX_PIN, DEBUG_TX_PIN);
  delay(1500);

  Serial.println();
  Serial.println("ATGM336H GPS scan test start");
  Serial.println("USB serial ready");
  Serial.printf("GPS UART: baud=%lu RX=%d TX=%d\n",
                static_cast<unsigned long>(GPS_BAUD),
                GPS_RX_PIN,
                GPS_TX_PIN);
  Serial.println("Wiring: ATGM336H TXD -> ESP32-C3 GPIO3, VCC -> 3V3, GND -> GND");

  GpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  lastStatusMs = millis();
  lastByteMs = millis();
}

void loop() {
  while (GpsSerial.available() > 0) {
    const char c = static_cast<char>(GpsSerial.read());
    gps.encode(c);
    lastByteMs = millis();
  }

  const uint32_t now = millis();

  if (now - lastStatusMs >= STATUS_PERIOD_MS) {
    lastStatusMs = now;
    printStatus();
  }

  if (now - lastByteMs >= NO_DATA_WARN_MS && gps.charsProcessed() == lastChars) {
    Serial.println();
    Serial.println("warning=no NMEA data received");
    Serial.println("check wiring, baud rate, power supply, and whether the module TXD is connected to GPIO3");
    lastByteMs = now;
  }

  lastChars = gps.charsProcessed();
  delay(10);
}
