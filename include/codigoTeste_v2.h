#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>

// ---------- CONFIG ----------
#define RX_PIN 16
#define TX_PIN 17
#define GPS_BAUD 9600

const char* apSsid = "GPS_AP";
const char* apPass = "gps12345"; // mínimo 8 chars

// intervalo de envio de status (ms)
const unsigned long STATUS_INTERVAL_MS = 500; // ajuste conforme necessidade
// se quiser enviar NMEA cru também, true
const bool SEND_NMEA_LINES = true;
// tamanho máximo de linha NMEA que aceitaremos (inclui CR/LF)
#define NMEA_LINE_MAX 128

// ---------- Objetos ----------
TinyGPSPlus gps;
HardwareSerial SerialGPS(2); // UART2 do ESP32

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Buffer de linha NMEA
static char nmeaLine[NMEA_LINE_MAX];
static size_t nmeaPos = 0;

// JSON document reutilizável (tamanho estimado; ajuste se adicionar campos)
StaticJsonDocument<256> jsonDoc;
char jsonBuffer[256];

// Tempo
unsigned long lastStatusMs = 0;

// ---------- HTML cliente simples (opcional) ----------
const char index_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>GPS Monitor</title>
  <style>
    :root {
      --gap: 12px;
      --panel-bg: #fafafa;
      --accent: #007bff;
      --max-width: 1100px; /* largura máxima em notebooks */
      --font-mono: "Courier New", Courier, monospace;
    }
    html,body {
      height: 100%;
      margin: 0;
      font-family: Arial, Helvetica, sans-serif;
      background: #fff;
      color: #111;
    }
    /* container centralizado com padding e limite de largura (bom para notebook + mobile) */
    .wrap {
      box-sizing: border-box;
      max-width: var(--max-width);
      margin: 0 auto;
      padding: var(--gap);
      height: 100vh; /* ocupa toda a viewport verticalmente */
      display: flex;
      flex-direction: column;
      gap: var(--gap);
    }
    header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 8px;
    }
    header h3 {
      margin: 0;
      font-size: 1.05rem;
    }
    /* área principal: o monitor (log) ocupa todo o espaço restante */
    main {
      flex: 1 1 auto;
      display: flex;
      flex-direction: column;
      gap: var(--gap);
    }
    /* O "monitor" é responsivo: usa flex para preencher e tem min/max height */
    #log {
      flex: 1 1 auto; /* cresce para ocupar o resto da altura */
      min-height: 120px;
      max-height: calc(100vh - 120px); /* evita crescer demais em telas gigantes */
      overflow: auto;
      border: 1px solid #ccc;
      padding: 10px;
      white-space: pre-wrap;
      font-family: var(--font-mono);
      background: var(--panel-bg);
      box-sizing: border-box;
      font-size: 0.95rem;
      line-height: 1.2;
      border-radius: 6px;
    }

    /* botão flutuante responsivo */
    #toBottomBtn {
      position: fixed;
      right: 18px;
      bottom: 18px;
      background: var(--accent);
      color: #fff;
      border: none;
      padding: 8px 12px;
      border-radius: 6px;
      box-shadow: 0 2px 6px rgba(0,0,0,0.2);
      cursor: pointer;
      display: none;
      z-index: 9999;
      font-size: 0.95rem;
    }

    /* comportamento em telas pequenas: botão menor e wrap padding reduzido */
    @media (max-width: 480px) {
      .wrap { padding: 8px; }
      #toBottomBtn { padding: 6px 10px; right: 10px; bottom: 10px; font-size: 0.85rem; }
      header h3 { font-size: 1rem; }
      #log { font-size: 0.9rem; }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <header>
      <h3>GPS Monitor</h3>
      <div id="statusSmall" aria-hidden="true" style="font-size:0.9rem; color:#555;">—</div>
    </header>

    <main>
      <div id="log" role="log" aria-live="polite" aria-atomic="false"></div>
    </main>
  </div>

  <button id="toBottomBtn" aria-label="Ir para o fim">Ir para o fim</button>

  <script>
    const el = document.getElementById('log');
    const btn = document.getElementById('toBottomBtn');
    const statusSmall = document.getElementById('statusSmall');
    let unread = 0;
    const SCROLL_THRESH = 12; // px - proximidade do final para considerar "no fim"

    // WebSocket
    const ws = new WebSocket('ws://' + location.hostname + '/ws');
    ws.onopen = () => add('[WS conectado]');
    ws.onclose = () => add('[WS fechado]');
    ws.onmessage = e => handleMsg(e.data);

    function handleMsg(msg) {
      // opcional: mostre um resumo no canto
      if (typeof msg === 'string' && msg.startsWith('{"type":"status"')) {
        try {
          const o = JSON.parse(msg);
          statusSmall.textContent = o.hasFix ? `Fix SATS:${o.sats||0}` : 'Sem fix';
        } catch(e) {
          // ignore if not valid JSON
        }
      }
      add(msg);
    }

    // botão "ir para o fim"
    btn.addEventListener('click', () => {
      scrollToBottom();
      unread = 0;
      updateButton();
    });

    // detecta scroll do usuário para resetar contador se voltar ao fim
    el.addEventListener('scroll', () => {
      if (isAtBottom()) {
        unread = 0;
        updateButton();
      }
    });

    function isAtBottom() {
      return (el.scrollHeight - el.scrollTop - el.clientHeight) <= SCROLL_THRESH;
    }

    function scrollToBottom() {
      // prefer smooth scroll se suportado
      try {
        el.scrollTo({ top: el.scrollHeight, behavior: 'smooth' });
      } catch(e) {
        el.scrollTop = el.scrollHeight;
      }
    }

    function updateButton() {
      if (unread > 0) {
        btn.style.display = 'block';
        btn.textContent = 'Ir para o fim (' + unread + ')';
      } else {
        btn.style.display = 'none';
      }
    }

    function add(t) {
      const atBottom = isAtBottom();
      // append linha preservando performance (usa TextNode)
      const node = document.createTextNode(t + '\n');
      el.appendChild(node);

      // opcional: limitar número de linhas para não crescer infinitamente
      const MAX_LINES = 2000;
      if (el.childNodes.length > MAX_LINES) {
        // remove blocos antigos em lote (ex: 200)
        for (let i = 0; i < 200; i++) {
          if (el.firstChild) el.removeChild(el.firstChild);
          else break;
        }
      }

      if (atBottom) {
        scrollToBottom();
      } else {
        unread++;
        updateButton();
      }
    }

    // Ajuste responsivo: recalcula se necessário em resize / orientationchange
    function onResize() {
      // se quiser fazer cálculos adicionais, adicione aqui
      // por enquanto, garantir que elemento ocupe espaço: scrollToBottom se estava no fim
      if (isAtBottom()) scrollToBottom();
    }
    window.addEventListener('resize', onResize);
    window.addEventListener('orientationchange', onResize);

    // Ao carregar, garante que o log ocupe espaço e comece no fim
    document.addEventListener('DOMContentLoaded', () => {
      scrollToBottom();
    });
  </script>
</body>
</html>
)rawliteral";


// ---------- Funções auxiliares ----------
void notifyAllClients(const char* data, size_t len) {
  // Constrói uma String com tamanho reservado para evitar fragmentação desnecessária
  String s;
  s.reserve(len + 1);
  s.concat(data, len);
  ws.textAll(s);
}

void notifyAllClients(const char* cstr) {
  // assume cstr terminado em '\0'
  ws.textAll(cstr);
}

// ---------- Eventos WebSocket ----------
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      {
        jsonDoc.clear();
        jsonDoc["type"] = "welcome";
        jsonDoc["baud"] = GPS_BAUD;
        size_t n = serializeJson(jsonDoc, jsonBuffer, sizeof(jsonBuffer));
        // jsonBuffer é '\0' terminado — envie como texto
        client->text(jsonBuffer);
      }
      break;
    case WS_EVT_DISCONNECT:
      break;
    case WS_EVT_DATA:
      // processar mensagem do cliente se precisar
      break;
    default:
      break;
  }
}

// ---------- Leitura do GPS: processa bytes quando disponíveis ----------
void processGpsBytes() {
  while (SerialGPS.available()) {
    int c = SerialGPS.read();
    // alimenta TinyGPS++
    gps.encode((char)c);

    // opcional: acumula linha NMEA e envia quando termina
    if (SEND_NMEA_LINES) {
      if (nmeaPos < NMEA_LINE_MAX - 1) {
        nmeaLine[nmeaPos++] = (char)c;
      } else {
        // overflow: descarta e reinicia
        nmeaPos = 0;
      }
      if (c == '\n') {
        // linha completa: envia como texto bruto
        nmeaLine[nmeaPos] = '\0';
        notifyAllClients(nmeaLine);
        nmeaPos = 0;
      }
    }
  }
}

// ---------- Envio periódico de status em JSON ----------
void sendStatusIfDue() {
  unsigned long now = millis();
  if (now - lastStatusMs < STATUS_INTERVAL_MS) return;
  lastStatusMs = now;

  jsonDoc.clear();
  jsonDoc["type"] = "status";
  jsonDoc["hasFix"] = gps.location.isValid();

  if (gps.location.isValid()) {
    jsonDoc["lat"] = gps.location.lat();
    jsonDoc["lon"] = gps.location.lng();
    // inclui age_ms apenas quando location é válida
    jsonDoc["age_ms"] = gps.location.age();
  } else {
    jsonDoc["lat"] = 0.0;
    jsonDoc["lon"] = 0.0;
  }

  if (gps.satellites.isValid()) jsonDoc["sats"] = gps.satellites.value();
  else jsonDoc["sats"] = 0;

  if (gps.hdop.isValid()) jsonDoc["hdop"] = gps.hdop.hdop();

  if (gps.time.isValid()) {
    jsonDoc["utc_hour"] = gps.time.hour();
    jsonDoc["utc_min"]  = gps.time.minute();
    jsonDoc["utc_sec"]  = gps.time.second();
  }

  size_t n = serializeJson(jsonDoc, jsonBuffer, sizeof(jsonBuffer));
  // jsonBuffer é terminado por '\0' — enviar como C-string
  ws.textAll(jsonBuffer);
}

// ---------- Setup / Loop ----------
void setup() {
  // Inicializa UART GPS
  SerialGPS.begin(GPS_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);

  // Inicia WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, apPass);
  IPAddress myIP = WiFi.softAPIP();

  // Servidor e WS
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();
}

void loop() {
  // processa bytes do GPS (sempre que existirem)
  processGpsBytes();

  // envia status em intervalo configurado (não bloqueante)
  sendStatusIfDue();

  // yield para RTOS
  yield();
}
