#include <Arduino.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>

// Pinos do ESP32 conectados ao GPS (ESP pins)
#define RX_PIN 16  // RX do ESP (conecta ao TX do GPS)
#define TX_PIN 17  // TX do ESP (conecta ao RX do GPS)

TinyGPSPlus gps;
HardwareSerial SerialGPS(2); // UART2 do ESP32

// Baud rates para tentar automaticamente
const uint32_t candidateBauds[] = {9600, 38400, 115200};
const size_t nBauds = sizeof(candidateBauds) / sizeof(candidateBauds[0]);

// Estado
uint32_t chosenBaud = 0;
bool protocolIsUBX = false;
unsigned long lastStatusMs = 0;

void tryAutoDetectBaud() {
  Serial.println("Auto-detectando baud do GPS...");
  // Buffer temporário para coleta de bytes durante teste
  const int READ_WINDOW_MS = 500; // tempo para cada baud
  for (size_t i = 0; i < nBauds; ++i) {
    uint32_t b = candidateBauds[i];
    Serial.print("Testando baud ");
    Serial.print(b);
    Serial.println(" ...");
    SerialGPS.begin(b, SERIAL_8N1, RX_PIN, TX_PIN);
    unsigned long t0 = millis();
    bool found = false;
    // lê por uma janela e busca por sinais de NMEA ($) ou UBX (0xB5 0x62)
    while (millis() - t0 < READ_WINDOW_MS) {
      if (SerialGPS.available()) {
        int c = SerialGPS.read();
        // detecta NMEA (caractere '$')
        if (c == '$') {
          Serial.print("  -> NMEA detectado no baud ");
          Serial.println(b);
          chosenBaud = b;
          found = true;
          break;
        }
        // detecta UBX inauguro (0xB5 0x62)
        if ((uint8_t)c == 0xB5) {
          // aguarda próximo byte com timeout curto
          unsigned long tpeek = millis();
          while (millis() - tpeek < 50 && SerialGPS.available() == 0) { /* espera */ }
          if (SerialGPS.available()) {
            int c2 = SerialGPS.read();
            if ((uint8_t)c2 == 0x62) {
              Serial.print("  -> UBX binário detectado no baud ");
              Serial.println(b);
              chosenBaud = b;
              protocolIsUBX = true;
              found = true;
              break;
            } else {
              // não UBX: continuar
            }
          }
        }
      }
    }
    if (found) break;
  }

  if (chosenBaud == 0) {
    // fallback: assumir 9600 e continuar
    chosenBaud = 9600;
    Serial.println("Nenhum protocolo detectado automaticamente. Usando fallback 9600.");
    Serial.println("Se não funcionar, tente alterar hardware/baud manualmente.");
    SerialGPS.begin(chosenBaud, SERIAL_8N1, RX_PIN, TX_PIN);
  } else {
    // reinicia porta com o baud escolhido (garante estado limpo)
    SerialGPS.begin(chosenBaud, SERIAL_8N1, RX_PIN, TX_PIN);
    Serial.print("Usando baud detectado: ");
    Serial.println(chosenBaud);
    if (protocolIsUBX) {
      Serial.println("OBS: parece que o módulo está enviando UBX (binário). TinyGPS++ não vai parsear UBX.");
      Serial.println("Considere reconfigurar o módulo para saída NMEA (usando u-center ou comandos UBX).");
    }
  }
}

// Encaminha bytes do GPS para o Serial USB (dump cru), e também alimenta TinyGPS++ ao mesmo tempo
void forwardAndParseGPS() {
  while (SerialGPS.available()) {
    int c = SerialGPS.read();
    // Mostra bytes legíveis e não legíveis
    if (c >= 32 && c <= 126) {
      Serial.write((char)c); // imprime caractere legível
    } else {
      // imprime hex para bytes não ASCII (útil para detectar UBX)
      Serial.print("<0x");
      if ((uint8_t)c < 16) Serial.print('0');
      Serial.print(String((uint8_t)c, HEX));
      Serial.print(">");
    }
    // encaminha também ao parser TinyGPS++
    gps.encode((char)c);

    // detecção contínua de UBX (quando você não viu na auto-detect)
    static bool sawB5 = false;
    if ((uint8_t)c == 0xB5) {
      sawB5 = true;
    } else if (sawB5 && (uint8_t)c == 0x62) {
      Serial.println();
      Serial.println("DETECÇÃO: sequência UBX (0xB5 0x62) observada (protocolo binário).");
      protocolIsUBX = true;
      sawB5 = false;
    } else {
      sawB5 = false;
    }
  }
}

void printStatusIfDue() {
  unsigned long now = millis();
  if (now - lastStatusMs < 2000) return; // a cada 2s
  lastStatusMs = now;

  Serial.println();
  Serial.println("------ STATUS GPS (a cada 2s) ------");
  Serial.print("Baud atual: ");
  Serial.println(chosenBaud);
  Serial.print("Protocolo aparenta ser UBX? ");
  Serial.println(protocolIsUBX ? "SIM" : "NAO");
  Serial.print("hasFix(): ");
  Serial.println(gps.location.isValid() ? "SIM" : "NAO");
  Serial.print("Satélites (TinyGPS++): ");
  if (gps.satellites.isValid()) Serial.println(gps.satellites.value());
  else Serial.println("N/A");

  if (gps.location.isValid()) {
    Serial.print("Latitude: ");
    Serial.println(gps.location.lat(), 6);
    Serial.print("Longitude: ");
    Serial.println(gps.location.lng(), 6);
    Serial.print("Age (ms): ");
    Serial.println(gps.location.age());
  } else {
    Serial.println("Latitude/Longitude inválidos no TinyGPS++");
  }

  if (gps.date.isValid() && gps.time.isValid()) {
    Serial.print("Data (UTC): ");
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
  } else {
    Serial.println("Data/Hora inválidos (NMEA não fornece ou não chegou ainda)");
  }
  Serial.println("------------------------------------");
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); } // garante Serial pronta (USB)
  Serial.println();
  Serial.println("=== Teste GPS NEO-M8N - Auto Detect + Debug ===");
  // tenta detectar baud e protocolo
  tryAutoDetectBaud();
  Serial.println("Entrando no loop principal. Você verá o dump cru e o status a cada 2s.");
  Serial.println("Se muitos <0x..> aparecerem no começo, pode ser binário UBX ou baud errado.");
  Serial.println();
}

void loop() {
  // 1) Encaminha bytes GPS para Serial USB e alimenta TinyGPS++
  forwardAndParseGPS();

  // 2) Mostra status a cada 2s
  printStatusIfDue();

  // 3) Pequena pausa não-bloqueante (libera CPU)
  delay(10);
}
