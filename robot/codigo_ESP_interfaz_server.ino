#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// --- CONFIGURACIÓN ---
const char* ssid = "snowden"; 
const char* password = "qwertyasdfghzxcvb54321"; 

// Serial2: Conexión con el Arduino Nano que controla los motores
#define RX2 16
#define TX2 17

// --- VARIABLES GLOBALES ---
WebServer server(80); // Servidor web en el puerto 80

// Mapeo de comandos de la web a comandos para el Nano (letra + valor)
// Puedes ajustar las velocidades aquí
#define SPEED_MOVE 200
#define SPEED_TURN 180

void handleCommand() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Metodo no permitido");
    return;
  }

  String body = server.arg("plain");
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    server.send(400, "text/plain", "JSON invalido");
    return;
  }

  const char* command = doc["command"];
  Serial.print("Comando web recibido: ");
  Serial.println(command);

  // Traduce el comando web a un comando para el Nano vía Serial2
  if (strcmp(command, "FORWARD") == 0)      { Serial2.printf("F%d\n", SPEED_MOVE); }
  else if (strcmp(command, "BACKWARD") == 0) { Serial2.printf("B%d\n", SPEED_MOVE); }
  else if (strcmp(command, "LEFT") == 0)     { Serial2.printf("L%d\n", SPEED_TURN); }
  else if (strcmp(command, "RIGHT") == 0)    { Serial2.printf("R%d\n", SPEED_TURN); }
  else if (strcmp(command, "BRUSH") == 0)    { Serial2.println("C1"); } // 'C' para Cepillo
  else if (strcmp(command, "COMPACTOR") == 0){ Serial2.println("P1"); } // 'P' para Compactador
  else if (strcmp(command, "STOP") == 0)     { Serial2.println("S0"); } // 'S' para Stop

  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RX2, TX2); // Inicia comunicación con el Nano

  // Conectar a WiFi
  Serial.print("Conectando a WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado!");
  Serial.print("IP del ESP32: ");
  Serial.println(WiFi.localIP());

  // Configurar rutas del servidor web
  server.on("/command", HTTP_POST, handleCommand);
  server.begin();
  Serial.println("Servidor web iniciado.");
}

void loop() {
  server.handleClient(); // Atiende las peticiones web
}