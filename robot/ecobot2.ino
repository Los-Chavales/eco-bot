#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// Configuración WiFi
const char* ssid = "DARTHNEO_WIFI";
const char* password = "DARTHNEO_PASSWORD";

// Pines del L298N (ajusta según tu conexión)
#define IN1  14  // Motor A
#define IN2  27
#define IN3  26  // Motor B
#define IN4  25
#define ENA  32  // PWM Motor A
#define ENB  33  // PWM Motor B

// Pin del relé para mecanismo de recolección
#define RELAY_PIN  4

// PWM
#define PWM_FREQ     1000
#define PWM_CHANNEL_A 0
#define PWM_CHANNEL_B 1
#define PWM_RESOLUTION 8
#define MOTOR_SPEED 200  // 0-255

WebServer server(80);

// Prototipos
void handleCommand();
void executeMovement(String command);
void stopMotors();
void moveForward();
void turnLeft();
void turnRight();
void activateCollector(bool on);

void setup() {
  Serial.begin(115200);

  // Configurar pines de motores
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);

  // PWM para velocidad de motores
  ledcSetup(PWM_CHANNEL_A, PWM_FREQ, PWM_RESOLUTION);
  ledcSetup(PWM_CHANNEL_B, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(ENA, PWM_CHANNEL_A);
  ledcAttachPin(ENB, PWM_CHANNEL_B);

  // Inicializar motores y relé apagados
  stopMotors();
  activateCollector(false);

  // Conexión WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Conectando a WiFi...");
  }
  Serial.println("WiFi conectado");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // Servidor web
  server.on("/command", HTTP_POST, handleCommand);
  server.begin();
}

void loop() {
  server.handleClient();
}

void handleCommand() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      server.send(400, "application/json", "{\"status\":\"error\",\"msg\":\"JSON inválido\"}");
      return;
    }
    String command = doc["command"];
    Serial.println("Comando recibido: " + command);
    executeMovement(command);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"msg\":\"Sin datos\"}");
  }
}

void executeMovement(String command) {
  if (command == "FORWARD") {
    moveForward();
    activateCollector(false);
  } else if (command == "LEFT") {
    turnLeft();
    activateCollector(false);
  } else if (command == "RIGHT") {
    turnRight();
    activateCollector(false);
  } else if (command == "STOP") {
    stopMotors();
    activateCollector(false);
  } else if (command == "COLLECT") {
    stopMotors();
    activateCollector(true);
    delay(1000); // Mantener el relé activado 1s (ajusta según mecanismo)
    activateCollector(false);
  } else {
    stopMotors();
    activateCollector(false);
  }
}

// Funciones de movimiento
void stopMotors() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  ledcWrite(PWM_CHANNEL_A, 0);
  ledcWrite(PWM_CHANNEL_B, 0);
}

void moveForward() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  ledcWrite(PWM_CHANNEL_A, MOTOR_SPEED);
  ledcWrite(PWM_CHANNEL_B, MOTOR_SPEED);
}

void turnLeft() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  ledcWrite(PWM_CHANNEL_A, MOTOR_SPEED);
  ledcWrite(PWM_CHANNEL_B, MOTOR_SPEED);
}

void turnRight() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  ledcWrite(PWM_CHANNEL_A, MOTOR_SPEED);
  ledcWrite(PWM_CHANNEL_B, MOTOR_SPEED);
}

// Relé para mecanismo de recolección
void activateCollector(bool on) {
  digitalWrite(RELAY_PIN, on ? HIGH : LOW);
}