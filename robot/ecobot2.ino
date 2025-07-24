#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// Configuración WiFi
const char* ssid = "snowden";
const char* password = "qwertyasdfghzxcvb54321";

// Configuración de IP Fija
IPAddress local_IP(192, 168, 0, 115);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 0, 1);

// Pines del nuevo L298N (compactador/recolección)
#define NEW_IN1  14   // Motor A (compactador)
#define NEW_IN2  12
#define NEW_ENA  13  // PWM Motor A
#define NEW_IN3  26  // Motor B (recolección)
#define NEW_IN4  25
#define NEW_ENB  33  // PWM Motor B

// Pines de servos compuerta frontal
#define SERVO1_PIN 19
#define SERVO2_PIN 18
#define SERVO3_PIN 5
#define SERVO4_PIN 32 // Nuevo servo MG996R

// PWM para nuevo L298N
#define NEW_PWM_FREQ     1000
#define NEW_PWM_CHANNEL_A 0
#define NEW_PWM_CHANNEL_B 1
#define NEW_PWM_RESOLUTION 8
#define COLLECTOR_SPEED 190      // 100% para recolección
#define COMPACTOR_SPEED 195      // 75% para compactador (255*0.75)

// PWM para Servos
#define SERVO_PWM_FREQ 50       // Frecuencia de 50Hz para servos
#define SERVO_PWM_RESOLUTION 10 // 10 bits de resolución (0-1023)
#define SERVO_MIN_PULSE_US 500  // 500us para 0 grados
#define SERVO_MAX_PULSE_US 2500 // 2500us para 180 grados

#define SERVO1_CHANNEL 3
#define SERVO2_CHANNEL 4
#define SERVO3_CHANNEL 5
#define SERVO4_CHANNEL 2 // Canal para el nuevo servo

// LED integrado
#define LED_BUILTIN 2 

WebServer server(80);

// Servidor Telnet para depuración
const int TELNET_PORT = 23; // Puerto estándar de Telnet
WiFiServer telnetServer(TELNET_PORT);
WiFiClient telnetClient;

// Serial2: TX2=GPIO17 → RX del Nano, RX2=GPIO16 ← TX del Nano
#define RX2 16
#define TX2 17

uint8_t speed = 85;

// Estado del sistema
enum SystemState {
  NORMAL,
  COLLECTING,
  WAITING_FOR_STOP,
  WAITING_FOR_COMPACTOR,
  COMPACTING
};
volatile SystemState systemState = NORMAL;

// Temporizador compactador
unsigned long compactTimerStart = 0;
const unsigned long compactDelay = 10000; // 10s
const unsigned long COLLECTOR_DURATION = 5000; // 5 segundos para el motor de recolección

// Tiempos del proceso de compactación
const unsigned long COMPACTOR_CLOSE_GATE_DELAY = 1500; // 1.5s para que la compuerta cierre
const unsigned long COMPACTOR_FORWARD_DURATION = 2700; // 4s motor compactador hacia adelante
const unsigned long COMPACTOR_WAIT_DURATION = 1000;    // 3s de espera entre movimientos del compactador
const unsigned long COMPACTOR_BACKWARD_DURATION = 2200; // 4s motor compactador hacia atrás
const unsigned long COMPACTOR_OPEN_GATE_DELAY = 1500;  // 1.5s para que la compuerta abra

// Temporizador recolección
unsigned long collectorTimerStart = 0;

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
void writeServoAngle(int pin, int channel, int angle);
void checkWiFiConnection();
void telnetPrintln(const String& msg);
void telnetPrint(const String& msg);

// Temporizador para mensajes de WiFi
unsigned long lastWiFiStatusPrint = 0;
const unsigned long WIFI_STATUS_INTERVAL = 10000; // 10 segundos
bool isWiFiReconnecting = false;

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RX2, TX2);

  // Configurar pines del nuevo L298N
  pinMode(NEW_IN1, OUTPUT);
  pinMode(NEW_IN2, OUTPUT);
  pinMode(NEW_IN3, OUTPUT);
  pinMode(NEW_IN4, OUTPUT);

  // PWM para velocidad de motores del nuevo L298N
  ledcSetup(NEW_PWM_CHANNEL_A, NEW_PWM_FREQ, NEW_PWM_RESOLUTION);
  ledcSetup(NEW_PWM_CHANNEL_B, NEW_PWM_FREQ, NEW_PWM_RESOLUTION);
  ledcAttachPin(NEW_ENA, NEW_PWM_CHANNEL_A);
  ledcAttachPin(NEW_ENB, NEW_PWM_CHANNEL_B);

  // Inicializar motores y relé apagados
  stopMotors();
  activateCollector(false);
  stopCompactorMotor();

  // Configurar PWM para servos
  ledcSetup(SERVO1_CHANNEL, SERVO_PWM_FREQ, SERVO_PWM_RESOLUTION);
  ledcAttachPin(SERVO1_PIN, SERVO1_CHANNEL);
  ledcSetup(SERVO2_CHANNEL, SERVO_PWM_FREQ, SERVO_PWM_RESOLUTION);
  ledcAttachPin(SERVO2_PIN, SERVO2_CHANNEL);
  ledcSetup(SERVO3_CHANNEL, SERVO_PWM_FREQ, SERVO_PWM_RESOLUTION);
  ledcAttachPin(SERVO3_PIN, SERVO3_CHANNEL);
  ledcSetup(SERVO4_CHANNEL, SERVO_PWM_FREQ, SERVO_PWM_RESOLUTION);
  ledcAttachPin(SERVO4_PIN, SERVO4_CHANNEL);
  openFrontGate();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Configurar la dirección IP estática
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS)) {
    Serial.println("Error al configurar STA Mode con IP estática.");
  }

  // Conexión WiFi inicial
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi...");

  // Esperar hasta conectar o agotar intentos
  int wifi_attempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_attempts < 20) {
    delay(1000);
    Serial.print(".");
    wifi_attempts++;
  }
  Serial.println("");
  if (WiFi.status() == WL_CONNECTED) {    
    Serial.print("WiFi inicializado correctamente. Conectado a ");
    Serial.println(ssid);
    Serial.print("Dirección IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Intensidad de señal: ");
    Serial.println(100 + WiFi.RSSI());
    digitalWrite(LED_BUILTIN, LOW);
  } else {
    Serial.print("No se pudo conectar a WiFi en el inicio. Estado: ");
    Serial.println(WiFi.status());
    digitalWrite(LED_BUILTIN, HIGH);
  }

  // Servidor web
  server.on("/command", HTTP_POST, handleCommand);
  server.begin();

  // Iniciar servidor Telnet
  telnetServer.begin();
  telnetServer.setNoDelay(true);
}

void loop() {
  server.handleClient();
  processTimers();
  checkWiFiConnection();

  // Manejar nuevas conexiones Telnet
  if (!telnetClient || !telnetClient.connected()) {
    if (telnetClient) {
      telnetClient.stop();
    }
    telnetClient = telnetServer.available();
    if (telnetClient) {
      Serial.println("Cliente Telnet conectado");
      telnetClient.println("Bienvenido al depurador Telnet del EcoBot!");
    }
  }

  // Leer datos del cliente Telnet (opcional, para comandos futuros)
  if (telnetClient && telnetClient.connected() && telnetClient.available()) {
    while (telnetClient.available()) {
      char c = telnetClient.read();
      // Puedes procesar comandos recibidos por Telnet aquí si es necesario
      Serial.write(c);
    }
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastWiFiStatusPrint >= WIFI_STATUS_INTERVAL) {
      digitalWrite(LED_BUILTIN, HIGH);
      telnetPrint("WiFi desconectado. Estado: ");
      telnetPrintln(String(WiFi.status())); 
      lastWiFiStatusPrint = currentMillis;
    }
    // Intentar reconectar
    isWiFiReconnecting = true;
    WiFi.disconnect();
    WiFi.reconnect();
    delay(500);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
    // Opcional: Imprimir IP si acaba de conectar o si no se ha impreso en mucho tiempo
    if (lastWiFiStatusPrint == 0 || isWiFiReconnecting || millis() - lastWiFiStatusPrint >= WIFI_STATUS_INTERVAL) {
      telnetPrint("WiFi sigue conectado. IP: ");
      telnetPrintln(WiFi.localIP().toString());
      telnetPrint("Intensidad de señal: ");
      telnetPrintln(String(100 + WiFi.RSSI()));
      isWiFiReconnecting = false;
      lastWiFiStatusPrint = millis();
    }
  }
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
    telnetPrintln("Comando recibido: " + command);


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
    turnRight();
    activateCollector(false);
  } else if (command == "RIGHT") {
    turnLeft();
    activateCollector(false);
  } else if (command == "STOP") {
    stopMotors();
    activateCollector(false);
  } else if (command == "COLLECT") {
    stopMotors();
    activateCollector(true);
    // Iniciar proceso de recolección no bloqueante
    systemState = COLLECTING;
    collectorTimerStart = millis();
  } else {
    stopMotors();
    activateCollector(false);
  }
}

// Funciones de movimiento
void stopMotors() {
  Serial2.write('S');
  speed = 0;
  Serial2.write(speed);
}

void moveForward() {
  Serial2.write('F');
  Serial2.write(speed);
}

void turnLeft() {
  Serial2.write('L');
  Serial2.write(speed);
}

void turnRight() {
  Serial2.write('R');
  Serial2.write(speed);
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
  writeServoAngle(SERVO2_PIN, SERVO2_CHANNEL, 0);
  writeServoAngle(SERVO3_PIN, SERVO3_CHANNEL, 170);
  writeServoAngle(SERVO1_PIN, SERVO1_CHANNEL, 100);
  writeServoAngle(SERVO4_PIN, SERVO4_CHANNEL, 180);
}

void closeFrontGate() {
  writeServoAngle(SERVO2_PIN, SERVO2_CHANNEL, 100);
  writeServoAngle(SERVO3_PIN, SERVO3_CHANNEL, 70);
  writeServoAngle(SERVO1_PIN, SERVO1_CHANNEL, 180);
  writeServoAngle(SERVO4_PIN, SERVO4_CHANNEL, 40);
}

// Lógica de temporizador y compactador
void writeServoAngle(int pin, int channel, int angle) {
  // Calcular el ancho de pulso en microsegundos
  // Mapear el ángulo (0-180) al rango de pulso (SERVO_MIN_PULSE_US a SERVO_MAX_PULSE_US)
  long pulseWidth = map(angle, 0, 180, SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);

  // Calcular el ciclo de trabajo para la resolución y frecuencia PWM
  // El período total en microsegundos es 1,000,000 / SERVO_PWM_FREQ
  // dutyCycle = (pulseWidth / (1,000,000 / SERVO_PWM_FREQ)) * (2^SERVO_PWM_RESOLUTION - 1)
  // Simplificando: dutyCycle = (pulseWidth * SERVO_PWM_FREQ * (2^SERVO_PWM_RESOLUTION - 1)) / 1,000,000
  uint32_t dutyCycle = (pulseWidth * (1 << SERVO_PWM_RESOLUTION)) / (1000000 / SERVO_PWM_FREQ);

  ledcWrite(channel, dutyCycle);
}

void processTimers() {
  static bool compactorStarted = false;
  static unsigned long compactorStepStart = 0;
  static int compactorStep = 0;

  // Proceso de recolección no bloqueante
  if (systemState == COLLECTING) {
    activateCollector(true);
    if (millis() - collectorTimerStart >= COLLECTOR_DURATION) {
      activateCollector(false);
      systemState = WAITING_FOR_STOP;
      // Reiniciar temporizador de compactación aquí si es necesario, o se hará en WAITING_FOR_STOP
    }
  }

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
      case 0: // Esperar a que cierre compuerta
        if (now - compactorStepStart > COMPACTOR_CLOSE_GATE_DELAY) {
          runCompactorMotor(true, COMPACTOR_SPEED); // Adelante (compactar)
          compactorStepStart = now;
          compactorStep = 1;
        }
        break;
      case 1: // Motor adelante
        if (now - compactorStepStart > COMPACTOR_FORWARD_DURATION) {
          stopCompactorMotor();
          compactorStepStart = now;
          compactorStep = 2;
        }
        break;
      case 2: // Esperar
        if (now - compactorStepStart > COMPACTOR_WAIT_DURATION) {
          runCompactorMotor(false, COMPACTOR_SPEED); // Atrás (volver a recolectar)
          compactorStepStart = now;
          compactorStep = 3;
        }
        break;
      case 3: // Motor atrás
        if (now - compactorStepStart > COMPACTOR_BACKWARD_DURATION) {
          stopCompactorMotor();
          openFrontGate();
          compactorStepStart = now;
          compactorStep = 4;
        }
        break;
      case 4: // Esperar a que abra compuerta
        if (now - compactorStepStart > COMPACTOR_OPEN_GATE_DELAY) {
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

void telnetPrintln(const String& msg) {
  Serial.println(msg);
  if (telnetClient && telnetClient.connected()) {
    telnetClient.println(msg);
  }
}

void telnetPrint(const String& msg) {
  Serial.print(msg);
  if (telnetClient && telnetClient.connected()) {
    telnetClient.print(msg);
  }
}