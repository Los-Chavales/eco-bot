#include <WiFi.h>

// Cambia estos datos por los de tu red WiFi
const char* ssid = "snowden";
const char* password = "qwertyasdfghzxcvb54321";

// Telnet server
WiFiServer telnetServer(23);
WiFiClient telnetClient;

// Serial2: TX2=GPIO17 → RX del Nano, RX2=GPIO16 ← TX del Nano
#define RX2 16
#define TX2 17

// LED integrado
#define LED_BUILTIN 2 

unsigned long lastTelnetInputTime = 0;
String telnetMessageQueue = "";

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RX2, TX2);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Conectar a WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConectado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  digitalWrite(LED_BUILTIN, LOW);

  // Iniciar Telnet
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  Serial.println("Servidor Telnet iniciado. Puerto 23");
}

String nanoLine = "";
unsigned long lastNanoData = millis();
unsigned long lastRssiReport = 0; // Para controlar el envío periódico de RSSI

void loop() {
  // Reconexión Wi-Fi automática
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado. Intentando reconectar...");
    digitalWrite(LED_BUILTIN, HIGH);
    WiFi.disconnect();
    WiFi.reconnect();
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      delay(1000);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nReconectado a WiFi!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      digitalWrite(LED_BUILTIN, LOW);
    } else {
      Serial.println("\nNo se pudo reconectar a WiFi.");
    }
  }

  // Reportar intensidad de señal Wi-Fi cada 5 segundos
  if (millis() - lastRssiReport > 5000) {
    lastRssiReport = millis();
    int rssi = WiFi.RSSI();
    Serial.print("[WiFi] Intensidad de señal (RSSI): ");
    Serial.print(rssi);
    Serial.println(" dBm");
  }

  // Aceptar nuevas conexiones Telnet
  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      if (telnetClient) telnetClient.stop();
      telnetClient = telnetServer.available();
      telnetClient.println("Bienvenido al robot por Telnet!");
      telnetClient.println("Comandos: F,B,L,R,S + [velocidad opcional 0-255] (ej: F200)");
      telnetClient.println("E1=activar evasión, E0=desactivar evasión");
    } else {
      // Rechazar conexiones adicionales
      WiFiClient newClient = telnetServer.available();
      newClient.println("Ya hay un cliente conectado.");
      newClient.stop();
    }
  }

  // Procesar comandos recibidos por Telnet
  if (telnetClient && telnetClient.connected() && telnetClient.available()) {
    lastTelnetInputTime = millis();
    String input = telnetClient.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      char cmd = toupper(input[0]);
      if (cmd == 'E') {
        // Activar/desactivar evasión de obstáculos
        uint8_t enable = (input.length() > 1 && input[1] == '1') ? 1 : 0;
        Serial2.write('E');
        Serial2.write(enable);
        telnetClient.printf("Evasion de obstaculos: %s\n", enable ? "ACTIVADA" : "DESACTIVADA");
        Serial.printf("Comando Telnet: E%d\n", enable);
      } else if (strchr("FBLRS", cmd)) {
        if (input.length() <= 1) {
          telnetClient.println("Comando inválido: velocidad requerida");
        } else {
          int v = input.substring(1).toInt();
          if (v < 0 || v > 255) {
            telnetClient.println("Comando inválido: velocidad fuera de rango (0-255)");
          } else {
            uint8_t speed = v;
            Serial2.write(cmd);
            Serial2.write(speed);
            telnetClient.printf("Enviado: %c %d\n", cmd, speed);
            Serial.printf("Comando Telnet: %c %d\n", cmd, speed);
          }
        }
      } else {
        telnetClient.print("Comando inválido: ");
        telnetClient.println(input);
      }
    }
  }

  // Leer comandos desde Serial USB (PC)
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      char cmd = toupper(input[0]);
      if (cmd == 'E') {
        uint8_t enable = (input.length() > 1 && input[1] == '1') ? 1 : 0;
        Serial2.write('E');
        Serial2.write(enable);
        Serial.printf("Enviado desde Serial: E%d\n", enable);
      } else if (strchr("FBLRS", cmd)) {
        if (input.length() <= 1) {
          Serial.println("Comando inválido desde Serial: velocidad requerida");
        } else {
          int v = input.substring(1).toInt();
          if (v < 0 || v > 255) {
            Serial.println("Comando inválido desde Serial: velocidad fuera de rango (0-255)");
          } else {
            uint8_t speed = v;
            Serial2.write(cmd);
            Serial2.write(speed);
            Serial.printf("Enviado desde Serial: %c %d\n", cmd, speed);
          }
        }
      } else {
        Serial.print("Comando inválido desde Serial: ");
        Serial.println(input);
      }
    }
  }

  // Leer datos del Nano y acumular mensajes en cola para Telnet
  while (Serial2.available()) {
    char c = Serial2.read();
    lastNanoData = millis();
    if (c == '\n' || c == '\r') {
      if (nanoLine.length() > 0) {
        String mensaje;
        if (nanoLine.startsWith("D:")) {
          mensaje = "[Nano] " + nanoLine + "\n";
        } else {
          mensaje = nanoLine + "\n";
        }
        Serial.print(mensaje); // Enviar a Serial siempre
        telnetMessageQueue += mensaje; // Acumular para Telnet
        nanoLine = "";
      }
    } else {
      nanoLine += c;
    }
  }

  // Enviar mensajes en cola a Telnet si han pasado 2 segundos sin entrada por Telnet
  if (telnetClient && telnetClient.connected() && telnetMessageQueue.length() > 0 &&
      (millis() - lastTelnetInputTime > 2000)) {
    telnetClient.print(telnetMessageQueue);
    telnetMessageQueue = "";
  }
}