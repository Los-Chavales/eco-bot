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

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RX2, TX2);

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

  // Iniciar Telnet
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  Serial.println("Servidor Telnet iniciado. Puerto 23");
}

void loop() {
  // Aceptar nuevas conexiones Telnet
  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      if (telnetClient) telnetClient.stop();
      telnetClient = telnetServer.available();
      telnetClient.println("Bienvenido al robot por Telnet!");
      telnetClient.println("Comandos: F,B,L,R,S (mayúscula=50%, minúscula=100%)");
    } else {
      // Rechazar conexiones adicionales
      WiFiClient newClient = telnetServer.available();
      newClient.println("Ya hay un cliente conectado.");
      newClient.stop();
    }
  }

  // Si hay un cliente conectado, leer comandos
  if (telnetClient && telnetClient.connected() && telnetClient.available()) {
    char cmd = telnetClient.read();
    // Solo aceptar letras válidas
    if (strchr("FBLRSfblrs", cmd)) {
      Serial2.write(cmd);
      telnetClient.print("Enviado: ");
      telnetClient.println(cmd);
      Serial.print("Comando Telnet: ");
      Serial.println(cmd);
    } else if (cmd >= 32 && cmd <= 126) {
      telnetClient.print("Comando inválido: ");
      telnetClient.println(cmd);
    }
  }

  // (Opcional) Mostrar datos del Nano hacia el cliente Telnet
  if (Serial2.available() && telnetClient && telnetClient.connected()) {
    char c = Serial2.read();
    telnetClient.write(c);
  }
}