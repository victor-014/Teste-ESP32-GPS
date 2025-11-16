#include <Arduino.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>

#define RX_PIN 16 // RX2 do ESP
#define TX_PIN 17 // TX2 do ESP

TinyGPSPlus gps;

HardwareSerial SerialGPS(2);

void setup() {
  Serial.begin(115200);
  SerialGPS.begin(9600, SERIAL_8N1, 16, 17); // Primeiro RX, depois TX

  Serial.println("Iniciando GPS NEO-M8N...");
}

void loop() {
  // Lê dados recebidos do GPS
  while (SerialGPS.available() > 0) {
    char c = SerialGPS.read();
    gps.encode(c); // Passa os dados para o parser da TinyGPS++
    
    // Quando tiver localização válida
    if (gps.location.isUpdated()) {
      Serial.print("Latitude: ");
      Serial.println(gps.location.lat(), 6);

      Serial.print("Longitude: ");
      Serial.println(gps.location.lng(), 6);

      Serial.print("Satélites: ");
      Serial.println(gps.satellites.value());

      Serial.print("Data: ");
      Serial.print(gps.date.day());
      Serial.print("/");
      Serial.print(gps.date.month());
      Serial.print("/");
      Serial.println(gps.date.year());

      Serial.print("Hora (UTC): ");
      Serial.print(gps.time.hour());
      Serial.print(":");
      Serial.print(gps.time.minute());
      Serial.print(":");
      Serial.println(gps.time.second());

      Serial.println("--------------------------");
    }
  }
}
