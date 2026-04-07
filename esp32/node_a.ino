#include <SPI.h>
#include <LoRa.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

// ── Pin definitions (match your wiring) ──────────────────────────
#define LORA_SCK   18
#define LORA_MOSI  23
#define LORA_MISO  19
#define LORA_CS     5
#define LORA_RST    4
#define LORA_DIO0   2

#define GPS_RX     16   // ESP32 RX2  ← GPS TX
#define GPS_TX     17   // ESP32 TX2  → GPS RX

#define NODE_ID    "A"  // ← Change to "B" on the second device

// ── Objects ──────────────────────────────────────────────────────
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);   // UART2

// ── Timing ───────────────────────────────────────────────────────
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 3000;  // ms between transmissions

// ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial);

  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  Serial.println("=== LoRa GPS Node " NODE_ID " ===");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(433E6)) {        // 433 MHz — change to 868E6 / 915E6 if needed
    Serial.println("[ERROR] LoRa init failed! Check wiring.");
    while (true);
  }

  LoRa.setSpreadingFactor(9);      // SF9 — good balance of range vs speed
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();

  Serial.println("[OK] LoRa ready. Waiting for GPS fix...");
}

// ─────────────────────────────────────────────────────────────────
void loop() {
  // Feed GPS characters into TinyGPSPlus
  while (gpsSerial.available())
    gps.encode(gpsSerial.read());

  // ── Transmit own coordinates every SEND_INTERVAL ──────────────
  if (millis() - lastSend >= SEND_INTERVAL) {
    lastSend = millis();
    sendGPS();
  }

  // ── Check for incoming LoRa packet ───────────────────────────
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    receivePacket();
  }
}

// ─────────────────────────────────────────────────────────────────
void sendGPS() {
  String payload;

  if (gps.location.isValid() && gps.location.age() < 2000) {
    payload  = String(NODE_ID) + ",";
    payload += String(gps.location.lat(), 6) + ",";
    payload += String(gps.location.lng(), 6) + ",";
    payload += String(gps.altitude.isValid() ? gps.altitude.meters() : 0.0, 1) + ",";
    payload += String(gps.satellites.isValid() ? gps.satellites.value() : 0);
  } else {
    payload = String(NODE_ID) + ",NO_FIX,NO_FIX,0,0";
  }

  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();

  Serial.print("[TX] Sent: ");
  Serial.println(payload);
}

// ─────────────────────────────────────────────────────────────────
void receivePacket() {
  String incoming = "";
  while (LoRa.available())
    incoming += (char)LoRa.read();

  int rssi = LoRa.packetRssi();
  float snr  = LoRa.packetSnr();

  // Parse: NodeID,lat,lng,alt,sats
  int c1 = incoming.indexOf(',');
  int c2 = incoming.indexOf(',', c1 + 1);
  int c3 = incoming.indexOf(',', c2 + 1);
  int c4 = incoming.indexOf(',', c3 + 1);

  if (c1 < 0) {
    Serial.println("[RX] Malformed packet: " + incoming);
    return;
  }

  String remoteID  = incoming.substring(0, c1);
  String latStr    = incoming.substring(c1 + 1, c2);
  String lngStr    = incoming.substring(c2 + 1, c3);
  String altStr    = incoming.substring(c3 + 1, c4);
  String satStr    = incoming.substring(c4 + 1);

  Serial.println("────────────────────────────────");
  Serial.println("[RX] Packet from Node " + remoteID);
  if (latStr == "NO_FIX") {
    Serial.println("  GPS: No fix yet");
  } else {
    Serial.println("  Latitude  : " + latStr + "°");
    Serial.println("  Longitude : " + lngStr + "°");
    Serial.println("  Altitude  : " + altStr + " m");
    Serial.println("  Satellites: " + satStr);
  }
  Serial.println("  RSSI      : " + String(rssi) + " dBm");
  Serial.println("  SNR       : " + String(snr,  1) + " dB");
  Serial.println("────────────────────────────────");
}