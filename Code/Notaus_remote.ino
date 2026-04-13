#include <WiFi.h>                 
// ---------- WLAN-Daten ----------
char ssid[] = "robot-net";
char password[] = "Q223tG4bb";

#include <WiFiUdp.h>

const char* raspberry_ip = "192.168.50.1";
const uint16_t raspberry_port = 5005;

WiFiUDP udp;

// ---------- Digitaler Eingang ----------
#define INPUT_PIN 26     //Notaus auf Pin 26

void setup() {
  Serial.begin(115200);
  delay(2000);

  // Pin als Eingang
  pinMode(INPUT_PIN, INPUT_PULLUP);
  
  
Serial.print("Verbinde mit WLAN...");
WiFi.mode(WIFI_STA);
WiFi.setSleep(false);

WiFi.begin(ssid, password);

unsigned long t0 = millis();
while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
  Serial.print(".");
  delay(250);

  static unsigned long last = 0;
  if (millis() - last > 1000) {
    last = millis();
    Serial.print(" status=");
    Serial.println((int)WiFi.status());
  }
}

Serial.println();
Serial.print("Final status=");
Serial.println((int)WiFi.status());   
}

void loop() {
  // Digitalpin lesen (LOW/HIGH)
bool state = digitalRead(INPUT_PIN);


uint8_t bitValue = state ? 1 : 0;

udp.beginPacket(raspberry_ip, raspberry_port);
udp.write(&bitValue, 1);
udp.endPacket();

delay(50);
}
