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
#define NEW_PWM_FREQ       1000
#define NEW_PWM_CHANNEL_A  0
#define NEW_PWM_CHANNEL_B  1
#define NEW_PWM_RESOLUTION 8
#define COLLECTOR_SPEED 190      // 100% para recolección
#define COMPACTOR_SPEED 210      // 75% para compactador (255*0.75)

// PWM para Servos
#define SERVO_PWM_FREQ       50      // Frecuencia de 50Hz para servos
#define SERVO_PWM_RESOLUTION 10      // 10 bits de resolución (0-1023)
#define SERVO_MIN_PULSE_US   500     // 500us para 0 grados
#define SERVO_MAX_PULSE_US   2500    // 2500us para 180 grados

#define SERVO1_CHANNEL 3
#define SERVO2_CHANNEL 4
#define SERVO3_CHANNEL 5
#define SERVO4_CHANNEL 2 // Canal para el nuevo servo

// LED integrado
#define LED_BUILTIN 2 

WebServer server(80);
// Servidor Telnet para depuración y control (re-habilitado para entrada)
const int TELNET_PORT = 23; // Puerto estándar de Telnet
WiFiServer telnetServer(TELNET_PORT);
WiFiClient telnetClient;

// Serial2: TX2=GPIO17 → RX del Nano, RX2=GPIO16 ← TX del Nano
#define RX2 16
#define TX2 17

uint8_t currentSpeed = 55; // Para avanzar y retroceder
uint8_t currentSpeed2 = 125; // Para girar

// Estado del sistema
enum SystemState {
  NORMAL,
  COLLECTING,
  WAITING_FOR_STOP,
  COMPACTING
};
volatile SystemState systemState = NORMAL;
bool pendingCompaction = false; // Bandera para indicar si hay basura pendiente de compactación

// Temporizador compactador
unsigned long compactTimerStart = 0;
const unsigned long compactDelay = 10000; // 10s de espera antes de compactar tras un STOP
const unsigned long COLLECTOR_DURATION = 5000; // 5 segundos para el motor de recolección

// Tiempos del proceso de compactación
const unsigned long COMPACTOR_CLOSE_GATE_DELAY = 1500; // 1.5s para que la compuerta cierre
const unsigned long COMPACTOR_FORWARD_DURATION = 2850; // 2.7s motor compactador hacia adelante
const unsigned long COMPACTOR_WAIT_DURATION = 1000; // 1s de espera entre movimientos del compactador
const unsigned long COMPACTOR_BACKWARD_DURATION = 2600; // 2.5s motor compactador hacia atrás
const unsigned long COMPACTOR_OPEN_GATE_DELAY = 1500; // 1.5s para que la compuerta abra

// Temporizador recolección
unsigned long collectorTimerStart = 0;

// Prototipos
void handleCommand();
void executeMovement(String command);
void stopMotors();
void moveForward();
void turnLeft();
void turnRight();
void activateCollector(bool on);
void runCompactorMotor(bool forward, uint8_t speed);
void stopCompactorMotor();
void openFrontGate();
void closeFrontGate();
void openBackGate();
void closeBackGate();
void processTimers();
void writeServoAngle(int pin, int channel, int angle);
void checkWiFiConnection();
void telnetPrintln(const String& msg);
void telnetPrint(const String& msg);
void processTelnetInput(); // Nuevo prototipo para manejar entrada Telnet
void processSerial2Input(); // Nuevo prototipo para manejar entrada Serial2 (desde Nano)

// Temporizador para mensajes de WiFi
unsigned long lastWiFiStatusPrint = 0;
const unsigned long WIFI_STATUS_INTERVAL = 10000; // 10 segundos
bool isWiFiReconnecting = false;
unsigned long lastTelnetInputTime = 0; // Para la lógica de la cola de mensajes Telnet
String telnetMessageQueue = ""; // Cola de mensajes para Telnet

// Variable para el estado de búsqueda del Nano
bool nano_is_searching = false; // 

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RX2, TX2); // Comunicación con Arduino Nano

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
  stopMotors(); // Asegurarse de que los motores del Nano estén detenidos
  activateCollector(false); // Detener motor de recolección
  stopCompactorMotor(); // Detener motor compactador

  // Configurar PWM para servos
  ledcSetup(SERVO1_CHANNEL, SERVO_PWM_FREQ, SERVO_PWM_RESOLUTION);
  ledcAttachPin(SERVO1_PIN, SERVO1_CHANNEL);
  ledcSetup(SERVO2_CHANNEL, SERVO_PWM_FREQ, SERVO_PWM_RESOLUTION);
  ledcAttachPin(SERVO2_PIN, SERVO2_CHANNEL);
  ledcSetup(SERVO3_CHANNEL, SERVO_PWM_FREQ, SERVO_PWM_RESOLUTION);
  ledcAttachPin(SERVO3_PIN, SERVO3_CHANNEL);
  ledcSetup(SERVO4_CHANNEL, SERVO_PWM_FREQ, SERVO_PWM_RESOLUTION);
  ledcAttachPin(SERVO4_PIN, SERVO4_CHANNEL);
  openFrontGate(); // Abrir compuerta al inicio
  closeBackGate(); // Cerrar compuerta trasera al inicio

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // LED encendido mientras conecta a WiFi

  // Configurar la dirección IP estática
  Serial.print("Configurando IP estática...");
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS)) {
    Serial.println("Error al configurar STA Mode con IP estática. Continuando con DHCP...");
    // Si falla la IP estática, intentar con DHCP
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  } else {
    Serial.println("IP estática configurada.");
  }

  // Conexión WiFi inicial
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");

  // Esperar hasta conectar o agotar intentos
  int wifi_attempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_attempts < 30) { // Aumentar intentos de conexión
    delay(500); // Reducir el delay para una conexión más rápida
    Serial.print(".");
    wifi_attempts++;
  }
  Serial.println(""); // Nueva línea después de los puntos de conexión
  if (WiFi.status() == WL_CONNECTED) {    
    Serial.print("WiFi inicializado correctamente. Conectado a ");
    Serial.println(ssid);
    Serial.print("Dirección IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Intensidad de señal (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    digitalWrite(LED_BUILTIN, LOW); // LED apagado cuando conectado
  } else {
    Serial.print("No se pudo conectar a WiFi en el inicio. Estado: ");
    Serial.println(WiFi.status());
    digitalWrite(LED_BUILTIN, HIGH); // LED encendido si no se conecta
  }

  // Servidor web
  server.on("/command", HTTP_POST, handleCommand);
  server.begin();
  Serial.println("Servidor Web iniciado en puerto 80");

  // Iniciar servidor Telnet
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  Serial.println("Servidor Telnet iniciado en puerto 23");
}

void loop() {
  server.handleClient(); // Maneja las peticiones del servidor web
  processTimers(); // Procesa los temporizadores para la máquina de estados
  checkWiFiConnection(); // Verifica y reconecta WiFi si es necesario

  // Manejar nuevas conexiones Telnet
  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      if (telnetClient) telnetClient.stop(); // Detener cliente anterior si existe
      telnetClient = telnetServer.available();
      Serial.println("Cliente Telnet conectado");
      telnetClient.println("Bienvenido al depurador Telnet del EcoBot!");
      telnetClient.println("Comandos para Nano: F,B,L,R,S + [velocidad 0-255] (ej: F200)");
      telnetClient.println("Comando para Nano: E1=activar evasión, E0=desactivar evasión");
      telnetClient.println("Comando para Nano: X=Iniciar busqueda de desechos, Z=Detener busqueda de desechos"); // [cite: 388]
      telnetClient.println("Comandos para ESP32: COLLECT, STOP (para recolección/compactación)");
    } else {
      // Rechazar conexiones adicionales
      WiFiClient newClient = telnetServer.available();
      newClient.println("Ya hay un cliente Telnet conectado.");
      newClient.stop();
    }
  }

  processTelnetInput();  // Procesa comandos recibidos por Telnet
  processSerial2Input(); // Procesa datos recibidos del Nano (Serial2)

  // Enviar mensajes en cola a Telnet si han pasado 2 segundos sin entrada por Telnet
  // Esto evita inundar el cliente Telnet si hay mucha salida del Nano
  if (telnetClient && telnetClient.connected() && telnetMessageQueue.length() > 0 &&
      (millis() - lastTelnetInputTime > 2000 || telnetMessageQueue.length() > 500)) { // O si la cola es muy grande
    telnetClient.print(telnetMessageQueue); // Enviar toda la cola acumulada
    telnetClient.flush(); // Asegurar envío completo
    telnetMessageQueue = ""; // Limpiar cola después de enviar
  }
}

// Función para verificar y reconectar WiFi
void checkWiFiConnection() {
  unsigned long currentMillis = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (!isWiFiReconnecting) { // Solo si no estamos ya en proceso de reconexión
      telnetPrintln("WiFi desconectado. Intentando reconectar...");
      digitalWrite(LED_BUILTIN, HIGH); // LED encendido para indicar desconexión
      isWiFiReconnecting = true;
      WiFi.disconnect();
      WiFi.reconnect();
      lastWiFiStatusPrint = currentMillis; // Reiniciar el temporizador para el estado de WiFi
    } else if (currentMillis - lastWiFiStatusPrint >= WIFI_STATUS_INTERVAL) {
      digitalWrite(LED_BUILTIN, HIGH);
      telnetPrint("WiFi reconectando... Estado: ");
      telnetPrintln(String(WiFi.status())); 
      lastWiFiStatusPrint = currentMillis;
    }
  } else {
    digitalWrite(LED_BUILTIN, LOW); // LED apagado si conectado
    if (isWiFiReconnecting || currentMillis - lastWiFiStatusPrint >= WIFI_STATUS_INTERVAL) {
      telnetPrint("WiFi conectado. IP: ");
      telnetPrintln(WiFi.localIP().toString());
      telnetPrint("Intensidad de señal: ");
      telnetPrint(String(100 + WiFi.RSSI()));
      telnetPrintln("%");
      isWiFiReconnecting = false;
      lastWiFiStatusPrint = currentMillis;
    }
  }
}

// Maneja los comandos recibidos por HTTP POST
void handleCommand() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      server.send(400, "application/json", "{\"status\":\"error\",\"msg\":\"JSON inválido\"}");
      telnetPrintln("ERROR: JSON inválido recibido.");
      return;
    }
    String command = doc["command"];
    telnetPrintln("Comando HTTP recibido: " + command);
    // Ignorar comandos de movimiento si estamos compactando
    if (systemState == COMPACTING) {
      server.send(200, "application/json", "{\"status\":\"busy\",\"msg\":\"Compactando. Comando ignorado.\"}");
      telnetPrintln("AVISO: Comando '" + command + "' ignorado, sistema compactando.");
      return;
    }

    // Adaptar los comandos X y Z para la búsqueda de desechos aquí también
    if (command == "START_SEARCH") { // Nuevo comando HTTP para iniciar búsqueda
      Serial2.write('X'); // [cite: 398]
      Serial2.write(0); // [cite: 399]
      nano_is_searching = true; // [cite: 400]
      telnetPrintln("Comando HTTP: Iniciar busqueda de desechos.");
      Serial.println("Comando HTTP: Iniciar busqueda de desechos.");
      server.send(200, "application/json", "{\"status\":\"ok\",\"msg\":\"Busqueda de desechos iniciada.\"}");
      return;
    } else if (command == "STOP_SEARCH") { // Nuevo comando HTTP para detener búsqueda
      Serial2.write('Z'); // [cite: 402]
      Serial2.write(0); // [cite: 403]
      nano_is_searching = false; // [cite: 404]
      telnetPrintln("Comando HTTP: Detener busqueda de desechos.");
      Serial.println("Comando HTTP: Detener busqueda de desechos.");
      server.send(200, "application/json", "{\"status\":\"ok\",\"msg\":\"Busqueda de desechos detenida.\"}");
      return;
    }

    executeMovement(command); // Ejecuta el comando
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"msg\":\"Sin datos en la petición\"}");
    telnetPrintln("ERROR: Petición HTTP sin datos.");
  }
}

// Ejecuta los comandos de movimiento y control
void executeMovement(String command) {
  // Si estamos esperando para compactar (tras STOP con basura pendiente)
  if (systemState == WAITING_FOR_STOP) {
    if (command == "STOP") {
      // Ya está esperando, ignora STOP repetidos
      telnetPrintln("AVISO: Ya en estado WAITING_FOR_STOP, STOP ignorado.");
      return;
    } else {
      // Cualquier otro comando cancela la espera de compactación
      telnetPrintln("AVISO: Comando '" + command + "' recibido, cancelando espera de compactación.");
      compactTimerStart = 0;
      systemState = NORMAL; // Volver a estado normal
      // pendingCompaction se mantiene si el usuario quiere compactar más tarde
    }
  }

  // Comandos que se envían al Nano
  if (command == "FORWARD") {
    moveForward();
    activateCollector(false); // Detener recolección al moverse
    telnetPrintln("Enviando F al Nano.");
    nano_is_searching = false; // [cite: 332] Stop search if direct movement command
  } else if (command == "LEFT") {
    turnLeft();
    activateCollector(false);
    telnetPrintln("Enviando L al Nano.");
    nano_is_searching = false; // [cite: 332] Stop search if direct movement command
  } else if (command == "RIGHT") {
    turnRight();
    activateCollector(false);
    telnetPrintln("Enviando R al Nano.");
    nano_is_searching = false; // [cite: 332] Stop search if direct movement command
  } else if (command == "STOP") {
    stopMotors();
    activateCollector(false);
    telnetPrintln("Enviando S al Nano y deteniendo recolección.");
    nano_is_searching = false; // [cite: 332] Stop search if direct movement command
    // Si hay basura pendiente, iniciar espera para compactar
    if (pendingCompaction) {
      if (systemState != WAITING_FOR_STOP) { // Evitar reiniciar el temporizador si ya está esperando
        compactTimerStart = millis();
        systemState = WAITING_FOR_STOP;
        telnetPrintln("Basura pendiente, iniciando espera de compactación.");
      }
    } else {
      telnetPrintln("No hay basura pendiente, solo deteniendo motores.");
    }
  } 
  // Comandos que controla directamente el ESP32
  else if (command == "COLLECT") {
    stopMotors(); // Detener el movimiento del robot principal
    activateCollector(true); // Iniciar proceso de recolección no bloqueante
    systemState = COLLECTING;
    collectorTimerStart = millis();
    pendingCompaction = true; // Marcar que hay basura pendiente
    telnetPrintln("Iniciando recolección. Basura marcada como pendiente.");
    nano_is_searching = false; // If collecting, stop autonomous search
  }  else if (command == "OPEN_BACK") { // Nuevo comando para abrir compuerta trasera
    openBackGate();
    telnetPrintln("Comando para abrir compuerta trasera recibido.");
  } else if (command == "CLOSE_BACK") { // Nuevo comando para cerrar compuerta trasera
    closeBackGate();
    telnetPrintln("Comando para cerrar compuerta trasera recibido.");
  } else if (command.startsWith("EVASION_")) { // Nuevo comando para evasión
    uint8_t enable = (command == "EVASION_ON") ? 1 : 0;
    Serial2.write('E');
    Serial2.write(enable);
    telnetClient.printf("Evasion de obstaculos (Nano): %s\n", enable ? "ACTIVADA" : "DESACTIVADA");
    Serial.printf("Comando Telnet/HTTP: E%d enviado al Nano\n", enable);
    if (!enable) { // [cite: 397] If evasion is disabled, stop autonomous search
        nano_is_searching = false;
    }
  } else {
    // Si el comando no es reconocido, detener todo
    stopMotors();
    activateCollector(false);
    telnetPrintln("Comando HTTP desconocido: " + command + ". Deteniendo todo.");
    nano_is_searching = false; // Stop search for unknown commands
  }
}

// Funciones de movimiento (envían comandos al Nano)
void stopMotors() {
  Serial2.write('S');
  Serial2.write(0); // Velocidad 0 para detener
  telnetPrintln("Comando S0 enviado al Nano.");
}

void moveForward() {
  Serial2.write('F');
  Serial2.write(currentSpeed);
  telnetPrintln("Comando F" + String(currentSpeed) + " enviado al Nano.");
}

void turnLeft() {
  Serial2.write('R');
  Serial2.write(currentSpeed2);
  telnetPrintln("Comando L" + String(currentSpeed2) + " enviado al Nano.");
}

void turnRight() {
  Serial2.write('L');
  Serial2.write(currentSpeed2);
  telnetPrintln("Comando R" + String(currentSpeed2) + " enviado al Nano.");
}

// Motor de recolección (motor B del nuevo L298N, controlado directamente por ESP32)
void activateCollector(bool on) {
  if (on) {
    digitalWrite(NEW_IN3, HIGH);
    digitalWrite(NEW_IN4, LOW);
    ledcWrite(NEW_PWM_CHANNEL_B, COLLECTOR_SPEED);
    telnetPrintln("Motor de recolección ACTIVADO.");
  } else {
    digitalWrite(NEW_IN3, LOW);
    digitalWrite(NEW_IN4, LOW);
    ledcWrite(NEW_PWM_CHANNEL_B, 0);
    telnetPrintln("Motor de recolección DESACTIVADO.");
  }
}

// Motor de compactador (motor A del nuevo L298N, controlado directamente por ESP32)
void runCompactorMotor(bool forward, uint8_t speed) {
  if (forward) {
    digitalWrite(NEW_IN1, HIGH);
    digitalWrite(NEW_IN2, LOW);
    telnetPrintln("Motor compactador hacia ADELANTE.");
  } else {
    digitalWrite(NEW_IN1, LOW);
    digitalWrite(NEW_IN2, HIGH);
    telnetPrintln("Motor compactador hacia ATRÁS.");
  }
  ledcWrite(NEW_PWM_CHANNEL_A, speed);
}

void stopCompactorMotor() {
  digitalWrite(NEW_IN1, LOW);
  digitalWrite(NEW_IN2, LOW);
  ledcWrite(NEW_PWM_CHANNEL_A, 0);
  telnetPrintln("Motor compactador DETENIDO.");
}

// Servos compuerta frontal
void openFrontGate() {
  telnetPrintln("Abriendo compuerta frontal...");
  writeServoAngle(SERVO4_PIN, SERVO4_CHANNEL, 180); // Servo compuerta frontal (compactador)
  delay(100); // Pequeño delay para que los servos se muevan
  telnetPrintln("Compuerta frontal ABIERTA.");
}

void closeFrontGate() {
  telnetPrintln("Cerrando compuerta frontal...");
  writeServoAngle(SERVO4_PIN, SERVO4_CHANNEL, 50);
  delay(100); // Pequeño delay para que los servos se muevan
  telnetPrintln("Compuerta frontal CERRADA.");
}

// Servos compuerta trasera
void openBackGate() {
  telnetPrintln("Abriendo compuerta trasera...");
  writeServoAngle(SERVO1_PIN, SERVO1_CHANNEL, 100); // Servo seguro compuerta lado derecho
  writeServoAngle(SERVO2_PIN, SERVO2_CHANNEL, 0); // Servo compuerta lado derecho
  writeServoAngle(SERVO3_PIN, SERVO3_CHANNEL, 170); // Servo compuerta lado izquierdo
  delay(100); // Pequeño delay para que los servos se muevan
  telnetPrintln("Compuerta trasera ABIERTA.");
  // The original code had motor movements here. These seem out of place for just opening the gate.
  // Assuming these are part of a compaction/ejection sequence that should be triggered by systemState
  // or explicit commands, not just opening the gate. Removing them from here for cleaner gate control.
  // delay(COMPACTOR_WAIT_DURATION);
  // runCompactorMotor(true, COMPACTOR_SPEED); // Adelante (compactar)
  // telnetPrintln("Expulsar");
  // delay(COMPACTOR_WAIT_DURATION);
  // runCompactorMotor(false, COMPACTOR_SPEED); // Atrás (volver a recolectar)
  // stopCompactorMotor();
}

void closeBackGate() {
  telnetPrintln("Cerrando compuerta trasera...");
  writeServoAngle(SERVO2_PIN, SERVO2_CHANNEL, 100);
  writeServoAngle(SERVO3_PIN, SERVO3_CHANNEL, 70);
  writeServoAngle(SERVO1_PIN, SERVO1_CHANNEL, 180);
  delay(100); // Pequeño delay para que los servos se muevan
  telnetPrintln("Compuerta trasera CERRADA.");
}

// Lógica de temporizador y compactador (máquina de estados)
void processTimers() {
  unsigned long currentMillis = millis();

  // Lógica de recolección
  if (systemState == COLLECTING) {
    if (currentMillis - collectorTimerStart >= COLLECTOR_DURATION) {
      activateCollector(false); // Detener el motor de recolección
      systemState = NORMAL; // O volver a WAITING_FOR_STOP si queremos compactar inmediatamente
      telnetPrintln("Recolección finalizada.");
      // Si pendingCompaction es true, se activa la espera para compactar en executeMovement("STOP")
    }
  }

  // Lógica de espera para compactación
  if (systemState == WAITING_FOR_STOP) {
    if (currentMillis - compactTimerStart >= compactDelay) {
      telnetPrintln("Iniciando compactación...");
      closeFrontGate(); // Cerrar compuerta frontal antes de compactar
      delay(COMPACTOR_CLOSE_GATE_DELAY);
      runCompactorMotor(true, COMPACTOR_SPEED); // Mover compactador hacia adelante
      telnetPrintln("Compactador adelante.");
      systemState = COMPACTING;
      compactTimerStart = currentMillis; // Reutilizar para temporizador de compactación
    }
  }

  // Lógica de compactación
  if (systemState == COMPACTING) {
    if (currentMillis - compactTimerStart >= COMPACTOR_FORWARD_DURATION) {
      stopCompactorMotor();
      telnetPrintln("Compactador detenido. Esperando para retroceder...");
      delay(COMPACTOR_WAIT_DURATION);
      runCompactorMotor(false, COMPACTOR_SPEED); // Mover compactador hacia atrás
      telnetPrintln("Compactador atrás.");
      compactTimerStart = currentMillis; // Reiniciar temporizador para movimiento hacia atrás
      systemState = NORMAL; // Asumir que después de retroceder, el estado vuelve a NORMAL
      pendingCompaction = false; // Basura compactada
    }
  }
}

// Envía un ángulo a un servo específico
void writeServoAngle(int pin, int channel, int angle) {
  // Mapear el ángulo (0-180) a un valor de ciclo de trabajo PWM (0-1023 para 10 bits)
  // Servos generalmente operan con pulsos de 1ms a 2ms dentro de un ciclo de 20ms (50Hz)
  // Resolución de 10 bits: 1023 pasos. 1 paso = 20ms / 1023 = 19.55 us/paso
  // Min pulso 500us -> 500/19.55 = 25.5 pasos
  // Max pulso 2500us -> 2500/19.55 = 127.8 pasos
  // Mapeo más preciso: map(angle, 0, 180, SERVOMIN_PULSE_WIDTH, SERVOMAX_PULSE_WIDTH)
  // Donde SERVOMIN_PULSE_WIDTH y SERVOMAX_PULSE_WIDTH son los valores de 0-1023
  int dutyCycle = map(angle, 0, 180, 25, 128); // Aproximado para SG90/MG996R
  ledcWrite(channel, dutyCycle);
}

// Funciones auxiliares para Telnet
void telnetPrintln(const String& msg) {
  if (telnetClient && telnetClient.connected()) {
    telnetMessageQueue += msg + "\n";
  }
  Serial.println(msg); // Siempre imprimir también en Serial USB
}

void telnetPrint(const String& msg) {
  if (telnetClient && telnetClient.connected()) {
    telnetMessageQueue += msg;
  }
  Serial.print(msg); // Siempre imprimir también en Serial USB
}

// Procesa comandos recibidos del cliente Telnet [cite: 316, 317, 318, 319, 320, 321, 322, 323, 324, 325, 326, 327, 328, 329, 330, 331, 332, 333]
void processTelnetInput() {
  if (telnetClient && telnetClient.connected() && telnetClient.available()) {
    lastTelnetInputTime = millis(); // Actualiza el tiempo de la última entrada Telnet
    String input = telnetClient.readStringUntil('\n'); // Lee la línea completa
    input.trim(); // Elimina espacios en blanco al inicio y final

    if (input.length() > 0) {
      char cmd = toupper(input[0]); // Convierte el primer carácter a mayúscula

      if (cmd == 'E') {
        // Comando para activar/desactivar la evasión de obstáculos en el Nano
        uint8_t enable = (input.length() > 1 && input[1] == '1') ? 1 : 0;
        Serial2.write('E'); // Envía el comando 'E'
        Serial2.write(enable); // Envía 1 o 0 como el byte de parámetro
        telnetClient.printf("Evasion de obstaculos (Nano): %s\n", enable ? "ACTIVADA" : "DESACTIVADA");
        Serial.printf("Comando Telnet: E%d\n", enable);
        if (!enable) {
            nano_is_searching = false; // Si se desactiva la evasión, también se detiene la búsqueda autónoma en el ESP32 [cite: 397]
        }
      } else if (cmd == 'X') { // Comando para iniciar la búsqueda de desechos en el Nano [cite: 398]
        Serial2.write('X'); // [cite: 398] Envía el comando 'X'
        Serial2.write(0); // [cite: 399] Envía un byte dummy (no se usa en el Nano para 'X')
        nano_is_searching = true; // [cite: 400] Actualiza el estado de búsqueda en el ESP32
        telnetClient.println("Comando Telnet: Iniciar busqueda de desechos."); // [cite: 401]
        Serial.println("Comando Telnet: Iniciar busqueda de desechos."); // [cite: 401]
      } else if (cmd == 'Z') { // Comando para detener la búsqueda de desechos en el Nano [cite: 402]
        Serial2.write('Z'); // [cite: 402] Envía el comando 'Z'
        Serial2.write(0); // [cite: 403] Envía un byte dummy
        nano_is_searching = false; // [cite: 404] Actualiza el estado de búsqueda en el ESP32
        telnetClient.println("Comando Telnet: Detener busqueda de desechos."); // [cite: 405]
        Serial.println("Comando Telnet: Detener busqueda de desechos."); // [cite: 405]
      } else if (strchr("FBLRS", cmd)) { // Comandos de movimiento directo (Forward, Backward, Left, Right, Stop) [cite: 329]
        if (input.length() <= 1) {
          telnetClient.println("Comando Telnet inválido: velocidad requerida"); // [cite: 330]
        } else {
          int v = input.substring(1).toInt(); // Extrae la velocidad del comando
          if (v < 0 || v > 255) {
            telnetClient.println("Comando Telnet inválido: velocidad fuera de rango (0-255)"); // [cite: 331]
          } else {
            uint8_t speed = v;
            Serial2.write(cmd); // Envía el comando de movimiento
            Serial2.write(speed); // Envía la velocidad
            nano_is_searching = false; // [cite: 332] Si se envía un comando directo de movimiento, se detiene la búsqueda autónoma en el ESP32
            telnetClient.printf("Enviado al Nano: %c %d\n", cmd, speed); // [cite: 333]
            Serial.printf("Comando Telnet: %c %d\n", cmd, speed); // [cite: 333]
          }
        }
      } else if (input == "COLLECT") {
        executeMovement("COLLECT");
      } else if (input == "STOP") {
        executeMovement("STOP");
      } else if (input == "OPEN_BACK") { // Comando Telnet para abrir compuerta trasera
        openBackGate();
        telnetClient.println("Compuerta trasera abierta.");
      } else if (input == "CLOSE_BACK") { // Comando Telnet para cerrar compuerta trasera
        closeBackGate();
        telnetClient.println("Compuerta trasera cerrada.");
      } else {
        telnetClient.print("Comando Telnet desconocido: ");
        telnetClient.println(input);
      }
    }
  }
}

// Procesa datos recibidos del Nano a través de Serial2 [cite: 375, 376, 377, 378, 379, 380, 381, 382, 383, 384, 385]
void processSerial2Input() {
  static String nanoLine = ""; // Buffer para la línea del Nano
  while (Serial2.available()) {
    char c = Serial2.read();
    // lastNanoData = millis(); // No se usa actualmente, pero podría ser útil para timeouts

    if (c == '\n' || c == '\r') {
      if (nanoLine.length() > 0) {
        String mensaje;
        if (nanoLine.startsWith("D:")) { // Si el Nano envía datos de depuración
          mensaje = "[Nano Debug] " + nanoLine + "\n";
          // If the Nano sends debug data and indicates search stopped due to obstacle
          if (nanoLine.indexOf("Obstaculo detectado durante busqueda!") != -1) { // [cite: 407]
              if (nano_is_searching) {
                  telnetClient.println("Nano: Busqueda detenida, posible obstaculo detectado o comando de parada."); // [cite: 407]
                  Serial.println("Nano: Busqueda detenida, posible obstaculo detectado o comando de parada."); // [cite: 407]
                  nano_is_searching = false; // El ESP32 también detiene su estado de búsqueda [cite: 407]
              }
          }
        } else if (nanoLine.equals("Obstaculo detectado durante busqueda!")) { // [cite: 408]
            mensaje = "[Nano] " + nanoLine + "\n";
            nano_is_searching = false; // Nano ha detenido la búsqueda por obstáculo [cite: 408]
        }
        else { // Otros mensajes del Nano
          mensaje = "[Nano] " + nanoLine + "\n";
        }
        Serial.print(mensaje); // Siempre imprimir en Serial USB
        telnetMessageQueue += mensaje; // Acumula el mensaje para enviarlo por Telnet
        nanoLine = ""; // Limpia el buffer de la línea del Nano
      }
    } else {
      nanoLine += c; // Añade el carácter al buffer
    }
  }

  // Enviar mensajes en cola a Telnet si han pasado 2 segundos sin entrada por Telnet
  if (telnetClient && telnetClient.connected() && telnetMessageQueue.length() > 0 &&
      (millis() - lastTelnetInputTime > 2000)) {
    telnetClient.print(telnetMessageQueue); // Envía toda la cola acumulada
    telnetClient.flush(); // Asegura que los datos se envíen de inmediato
    telnetMessageQueue = ""; // Limpia el buffer
  }
}