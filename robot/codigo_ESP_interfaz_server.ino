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
// Motor principal del Compactador
#define COMPACTOR_MAIN_IN1 25     // Motor A (compactador - principal)
#define COMPACTOR_MAIN_IN2 26
#define COMPACTOR_MAIN_ENA 33     // PWM Motor A

// Nuevo Motor DC para la acción del compactador (simula el movimiento del servo)
#define COMPACTOR_ACTION_IN1 21  // Pines para el nuevo motor DC
#define COMPACTOR_ACTION_IN2 22   // (Ajusta estos pines a tus conexiones reales en el ESP32)
#define COMPACTOR_ACTION_ENA 23    // PWM para el nuevo motor DC

// Motor de Recolección (Cepillo)
#define BRUSH_IN1 27         // Motor B (recolección/cepillo)
#define BRUSH_IN2 14
#define BRUSH_ENB 32         // PWM Motor B

// --- VARIABLES GLOBALES ---
WebServer server(80); // Servidor web en el puerto 80

// Mapeo de comandos de la web a comandos para el Nano (letra + valor)
// Puedes ajustar las velocidades aquí
#define SPEED_MOVE 200
#define SPEED_TURN 180
#define SPEED_COMPACTOR_MAIN 255  // Velocidad para el motor principal del compactador
#define SPEED_BRUSH 200           // Velocidad para el cepillo

// Parámetros para el nuevo motor DC del compactador
#define SPEED_COMPACTOR_ACTION 180 // Velocidad para el motor de acción del compactador
#define COMPACTOR_ACTION_DURATION 1500 // Duración del movimiento del motor de acción en ms (ajusta según necesites)
#define COMPACTOR_DELAY_AFTER_MAIN 500 // Retraso entre el motor principal y el de acción en ms

// --- Funciones para Motores (gestionadas por ESP32) ---
void setMotorESP32(int in1, int in2, int ena, bool forward, uint8_t vel) {
  digitalWrite(in1, forward ? HIGH : LOW);
  digitalWrite(in2, forward ? LOW : HIGH);
  analogWrite(ena, vel);
}

void activateCompactorMain(uint8_t vel) {
  // Activa el motor principal del compactador en una dirección
  setMotorESP32(COMPACTOR_MAIN_IN1, COMPACTOR_MAIN_IN2, COMPACTOR_MAIN_ENA, true, vel);
  Serial.println("Compactador Principal Activado");
}

void stopCompactorMain() {
  // Detiene el motor principal del compactador
  setMotorESP32(COMPACTOR_MAIN_IN1, COMPACTOR_MAIN_IN2, COMPACTOR_MAIN_ENA, true, 0);
  Serial.println("Compactador Principal Detenido");
}

void activateCompactorAction(uint8_t vel) {
  // Activa el motor DC para la acción de compactación (simula el servo)
  setMotorESP32(COMPACTOR_ACTION_IN1, COMPACTOR_ACTION_IN2, COMPACTOR_ACTION_ENA, true, vel); // Asume una dirección
  Serial.println("Compactador Acción Activado");
}

void stopCompactorAction() {
  // Detiene el motor DC de la acción de compactación
  setMotorESP32(COMPACTOR_ACTION_IN1, COMPACTOR_ACTION_IN2, COMPACTOR_ACTION_ENA, true, 0);
  Serial.println("Compactador Acción Detenido");
}

void activateCompactorSequence() {
  // 1. Activa el motor principal del compactador
  activateCompactorMain(SPEED_COMPACTOR_MAIN);
  delay(COMPACTOR_DELAY_AFTER_MAIN); // Espera un poco para que el motor principal actúe

  // 2. Activa el motor de acción (simulando el movimiento del servo)
  activateCompactorAction(SPEED_COMPACTOR_ACTION);
  delay(COMPACTOR_ACTION_DURATION); // Mantiene el motor de acción girando por la duración
  stopCompactorAction();            // Detiene el motor de acción

  // 3. Detiene el motor principal del compactador (si la secuencia lo requiere, o déjalo correr si es continuo)
  // Depende de cómo quieras que funcione el compactador. Si el motor principal es solo para iniciar, deténlo.
  // Si el motor principal debe seguir girando, no lo detengas aquí.
  stopCompactorMain(); // Detenemos el motor principal después de la acción.
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
  else if (strcmp(command, "COMPACTOR") == 0){ activateCompactorSequence(); } // ESP32 controla la secuencia del compactador
  else if (strcmp(command, "STOP") == 0)     {
    Serial2.println("S0"); // Manda a detener los motores de movilidad al Nano
    stopBrush();           // Detiene el cepillo si está activo
    stopCompactorMain();   // Detiene el motor principal del compactador
    stopCompactorAction(); // Detiene el motor de acción del compactador
  }

  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RX2, TX2); // Inicia comunicación con el Nano

  // Configurar pines de los motores adicionales como OUTPUT
  pinMode(COMPACTOR_MAIN_IN1, OUTPUT);
  pinMode(COMPACTOR_MAIN_IN2, OUTPUT);
  pinMode(COMPACTOR_MAIN_ENA, OUTPUT);

  pinMode(COMPACTOR_ACTION_IN1, OUTPUT);
  pinMode(COMPACTOR_ACTION_IN2, OUTPUT);
  pinMode(COMPACTOR_ACTION_ENA, OUTPUT);

  pinMode(BRUSH_IN1, OUTPUT);
  pinMode(BRUSH_IN2, OUTPUT);
  pinMode(BRUSH_ENB, OUTPUT);

  // Asegurarse de que todos los motores adicionales estén apagados al inicio
  stopBrush();
  stopCompactorMain();
  stopCompactorAction();

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