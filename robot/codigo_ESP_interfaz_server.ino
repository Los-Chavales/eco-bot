#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// --- CONFIGURACIÓN ---
const char* ssid = "snowden";
const char* password = "qwertyasdfghzxcvb54321";

// Serial2: Conexión con el Arduino Nano que controla los motores de movilidad
#define RX2 16
#define TX2 17

// --- Pines Motores Adicionales (Controlados por ESP32) ---
#define COMPACTOR_IN1 25     // Motor A (compactador)
#define COMPACTOR_IN2 26
#define COMPACTOR_ENA 33     // PWM Motor A

#define BRUSH_IN1 27         // Motor B (recolección/cepillo)
#define BRUSH_IN2 14
#define BRUSH_ENB 32         // PWM Motor B

// --- VARIABLES GLOBALES ---
WebServer server(80); // Servidor web en el puerto 80

// Mapeo de comandos de la web a comandos para el Nano (letra + valor)
// Puedes ajustar las velocidades aquí
#define SPEED_MOVE 200
#define SPEED_TURN 180
#define SPEED_COMPACTOR 255  // Velocidad para el compactador (directamente en ESP32)
#define SPEED_BRUSH 200      // Velocidad para el cepillo (directamente en ESP32)

// --- Funciones para Motores Adicionales (gestionadas por ESP32) ---
void setMotorESP32(int in1, int in2, int ena, bool forward, uint8_t vel) {
  digitalWrite(in1, forward ? HIGH : LOW);
  digitalWrite(in2, forward ? LOW : HIGH);
  analogWrite(ena, vel);
}

void activateCompactor(uint8_t vel) {
  // Asumiendo una dirección de giro para el compactador
  setMotorESP32(COMPACTOR_IN1, COMPACTOR_IN2, COMPACTOR_ENA, true, vel);
  Serial.println("Compactador Activado");
}

void stopCompactor() {
  setMotorESP32(COMPACTOR_IN1, COMPACTOR_IN2, COMPACTOR_ENA, true, 0); // Detener
  Serial.println("Compactador Detenido");
}

void activateBrush(uint8_t vel) {
  // Asumiendo una dirección de giro para el cepillo
  setMotorESP32(BRUSH_IN1, BRUSH_IN2, BRUSH_ENB, true, vel);
  Serial.println("Cepillo Activado");
}

void stopBrush() {
  setMotorESP32(BRUSH_IN1, BRUSH_IN2, BRUSH_ENB, true, 0); // Detener
  Serial.println("Cepillo Detenido");
}


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
  // O ejecuta la acción directamente si es para el ESP32
  if (strcmp(command, "FORWARD") == 0)      { Serial2.printf("F%d\n", SPEED_MOVE); }
  else if (strcmp(command, "BACKWARD") == 0) { Serial2.printf("B%d\n", SPEED_MOVE); }
  else if (strcmp(command, "LEFT") == 0)     { Serial2.printf("L%d\n", SPEED_TURN); }
  else if (strcmp(command, "RIGHT") == 0)    { Serial2.printf("R%d\n", SPEED_TURN); }
  else if (strcmp(command, "BRUSH") == 0)    { activateBrush(SPEED_BRUSH); }      // ESP32 controla el cepillo
  else if (strcmp(command, "COMPACTOR") == 0){ activateCompactor(SPEED_COMPACTOR); } // ESP32 controla el compactador
  else if (strcmp(command, "STOP") == 0)     {
    Serial2.println("S0"); // Manda a detener los motores de movilidad al Nano
    stopBrush();           // Detiene el cepillo si está activo
    stopCompactor();       // Detiene el compactador si está activo
  }

  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RX2, TX2); // Inicia comunicación con el Nano

  // Configurar pines de los motores adicionales como OUTPUT
  pinMode(COMPACTOR_IN1, OUTPUT);
  pinMode(COMPACTOR_IN2, OUTPUT);
  pinMode(COMPACTOR_ENA, OUTPUT);
  pinMode(BRUSH_IN1, OUTPUT);
  pinMode(BRUSH_IN2, OUTPUT);
  pinMode(BRUSH_ENB, OUTPUT);

  // Asegurarse de que los motores adicionales estén apagados al inicio
  stopBrush();
  stopCompactor();

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