#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <driver/ledc.h> // <-- Añadido para funciones LEDC del ESP32

// Configuración WiFi
const char* ssid = "snowden";
const char* password = "qwertyasdfghzxcvb54321";

// Pines del L298N original (desplazamiento)
#define IN1  27  // Motor A
#define IN2  26
#define IN3  25  // Motor B
#define IN4  33
#define ENA  13  // PWM Motor A
#define ENB  14  // PWM Motor B

// Pines del nuevo L298N (compactador/recolección)
#define NEW_IN1  21   // Motor A (compactador)
#define NEW_IN2  19
#define NEW_ENA  22  // PWM Motor A
#define NEW_IN3  18  // Motor B (recolección)
#define NEW_IN4  5
#define NEW_ENB  23  // PWM Motor B

// Pines de servos compuerta frontal
#define SERVO1_PIN 34
#define SERVO2_PIN 35
#define SERVO3_PIN 32

// PWM
#define PWM_FREQ     1000
#define PWM_CHANNEL_A 0
#define PWM_CHANNEL_B 1
#define PWM_RESOLUTION 8
#define MOTOR_SPEED 130  // 0-255

// PWM para nuevo L298N
#define NEW_PWM_FREQ     1000
#define NEW_PWM_CHANNEL_A 2
#define NEW_PWM_CHANNEL_B 3
#define NEW_PWM_RESOLUTION 8
#define COLLECTOR_SPEED 255      // 100% para recolección
#define COMPACTOR_SPEED 191      // 75% para compactador (255*0.75)

WebServer server(80);

// Servos
Servo servo1, servo2, servo3;

// Estado del sistema
enum SystemState {
  NORMAL,
  WAITING_FOR_STOP,
  WAITING_FOR_COMPACTOR,
  COMPACTING
};
volatile SystemState systemState = NORMAL;

// Temporizador compactador
unsigned long compactTimerStart = 0;
const unsigned long compactDelay = 10000; // 10s

// Prototipos
void handleCommand();
void executeMovement(String command);
void stopMotors();
void moveForward();
void turnLeft();
void turnRight();
void activateCollector(bool on); // Redefinida para el nuevo L298N
void runCompactorMotor(bool forward, uint8_t speed);
void stopCompactorMotor();
void openFrontGate();
void closeFrontGate();
void processTimers();

void setup() {
  Serial.begin(115200);

  // Configurar pines de motores
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  // PWM para velocidad de motores
  ledcSetup(PWM_CHANNEL_A, PWM_FREQ, PWM_RESOLUTION);
  ledcSetup(PWM_CHANNEL_B, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(ENA, PWM_CHANNEL_A);
  ledcAttachPin(ENB, PWM_CHANNEL_B);

  // Configurar pines del nuevo L298N
  pinMode(NEW_IN1, OUTPUT);
  pinMode(NEW_IN2, OUTPUT);
  pinMode(NEW_IN3, OUTPUT);
  pinMode(NEW_IN4, OUTPUT);

  // PWM para nuevo L298N
  ledcSetup(NEW_PWM_CHANNEL_A, NEW_PWM_FREQ, NEW_PWM_RESOLUTION);
  ledcSetup(NEW_PWM_CHANNEL_B, NEW_PWM_FREQ, NEW_PWM_RESOLUTION);
  ledcAttachPin(NEW_ENA, NEW_PWM_CHANNEL_A);
  ledcAttachPin(NEW_ENB, NEW_PWM_CHANNEL_B);

  // Inicializar motores y relé apagados
  stopMotors();
  activateCollector(false);
  stopCompactorMotor();

  // Inicializar servos
  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  servo3.attach(SERVO3_PIN);
  openFrontGate();

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
  processTimers();
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

    // Ignorar comandos durante compactación
    if (systemState == COMPACTING) {
      server.send(200, "application/json", "{\"status\":\"busy\",\"msg\":\"Compactando\"}");
      return;
    }

    executeMovement(command);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"msg\":\"Sin datos\"}");
  }
}

void executeMovement(String command) {
  // Si estamos esperando STOP tras COLLECT
  if (systemState == WAITING_FOR_STOP) {
    if (command == "STOP") {
      // Iniciar temporizador para compactador
      compactTimerStart = millis();
      systemState = WAITING_FOR_COMPACTOR;
      stopMotors();
      return;
    } else {
      // Cancelar espera si llega otro comando
      systemState = NORMAL;
    }
  }

  // Si estamos esperando activar compactador, cancelar si llega otro comando
  if (systemState == WAITING_FOR_COMPACTOR) {
    if (command != "STOP") {
      systemState = NORMAL;
      compactTimerStart = 0;
    }
  }

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
    delay(5000); // Mantener el motor de recolección 1s
    activateCollector(false);
    // Esperar STOP para activar compactador
    systemState = WAITING_FOR_STOP;
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

// Motor de recolección (motor B del nuevo L298N)
void activateCollector(bool on) {
  if (on) {
    digitalWrite(NEW_IN3, HIGH);
    digitalWrite(NEW_IN4, LOW);
    ledcWrite(NEW_PWM_CHANNEL_B, COLLECTOR_SPEED);
  } else {
    digitalWrite(NEW_IN3, LOW);
    digitalWrite(NEW_IN4, LOW);
    ledcWrite(NEW_PWM_CHANNEL_B, 0);
  }
}

// Motor de compactador (motor A del nuevo L298N)
void runCompactorMotor(bool forward, uint8_t speed) {
  if (forward) {
    digitalWrite(NEW_IN1, HIGH);
    digitalWrite(NEW_IN2, LOW);
  } else {
    digitalWrite(NEW_IN1, LOW);
    digitalWrite(NEW_IN2, HIGH);
  }
  ledcWrite(NEW_PWM_CHANNEL_A, speed);
}

void stopCompactorMotor() {
  digitalWrite(NEW_IN1, LOW);
  digitalWrite(NEW_IN2, LOW);
  ledcWrite(NEW_PWM_CHANNEL_A, 0);
}

// Servos compuerta frontal
void openFrontGate() {
  servo1.write(90);
  servo2.write(90);
  servo3.write(90);
}

void closeFrontGate() {
  servo1.write(0);
  servo2.write(0);
  servo3.write(0);
}

// Lógica de temporizador y compactador
void processTimers() {
  static bool compactorStarted = false;
  static unsigned long compactorStepStart = 0;
  static int compactorStep = 0;

  // Esperando para activar compactador
  if (systemState == WAITING_FOR_COMPACTOR && compactTimerStart > 0) {
    if (millis() - compactTimerStart >= compactDelay) {
      // Iniciar compactador
      systemState = COMPACTING;
      compactorStarted = true;
      compactorStep = 0;
      compactorStepStart = millis();
      closeFrontGate();
    }
  }

  // Proceso de compactación
  if (systemState == COMPACTING && compactorStarted) {
    unsigned long now = millis();
    switch (compactorStep) {
      case 0: // Esperar a que cierre compuerta (1.5s)
        if (now - compactorStepStart > 1500) {
          runCompactorMotor(true, COMPACTOR_SPEED); // Adelante (compactar)
          compactorStepStart = now;
          compactorStep = 1;
        }
        break;
      case 1: // Motor adelante 2s
        if (now - compactorStepStart > 2000) {
          stopCompactorMotor();
          compactorStepStart = now;
          compactorStep = 2;
        }
        break;
      case 2: // Esperar 3s
        if (now - compactorStepStart > 3000) {
          runCompactorMotor(false, COMPACTOR_SPEED); // Atrás (volver a recolectar)
          compactorStepStart = now;
          compactorStep = 3;
        }
        break;
      case 3: // Motor atrás 2s
        if (now - compactorStepStart > 2000) {
          stopCompactorMotor();
          openFrontGate();
          compactorStepStart = now;
          compactorStep = 4;
        }
        break;
      case 4: // Esperar a que abra compuerta (1.5s)
        if (now - compactorStepStart > 1500) {
          // Fin del proceso
          systemState = NORMAL;
          compactorStarted = false;
          compactorStep = 0;
          compactTimerStart = 0;
        }
        break;
    }
  }
}