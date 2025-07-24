#include <WiFi.h>

// Configuración WiFi
const char* ssid = "GRATITUD";
const char* password = "gracias11";

// Configuración de IP Fija
IPAddress local_IP(192, 168, 0, 115);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 0, 1);

// Pines del nuevo L298N (compactador/recolección)
#define NEW_IN1   14   // Motor A (compactador)
#define NEW_IN2   12
#define NEW_ENA   13   // PWM Motor A
#define NEW_IN3   26   // Motor B (recolección)
#define NEW_IN4   25
#define NEW_ENB   33   // PWM Motor B

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
#define COLLECTOR_SPEED    190       // 100% para recolección (ajustado a 8-bit, 255 es 100%)
#define COMPACTOR_SPEED    195       // 75% para compactador (255*0.75, ajustado a 8-bit)

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

// Servidor Telnet para depuración y control
const int TELNET_PORT = 23; // Puerto estándar de Telnet
WiFiServer telnetServer(TELNET_PORT);
WiFiClient telnetClient;

// Serial2: TX2=GPIO17 → RX del Nano, RX2=GPIO16 ← TX del Nano
#define RX2 16
#define TX2 17

uint8_t currentSpeed = 85; // Velocidad predeterminada para el movimiento del robot

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
const unsigned long COMPACTOR_FORWARD_DURATION = 2700; // 2.7s motor compactador hacia adelante
const unsigned long COMPACTOR_WAIT_DURATION = 1000;    // 1s de espera entre movimientos del compactador
const unsigned long COMPACTOR_BACKWARD_DURATION = 2500; // 2.5s motor compactador hacia atrás
const unsigned long COMPACTOR_OPEN_GATE_DELAY = 1500;  // 1.5s para que la compuerta abra

// Temporizador recolección
unsigned long collectorTimerStart = 0;

// Prototipos
void executeCommand(String command); // Función unificada para ejecutar comandos
void stopMotors();
void moveForward();
void turnLeft();
void turnRight();
void activateCollector(bool on);
void runCompactorMotor(bool forward, uint8_t speed);
void stopCompactorMotor();
void openFrontGate();
void closeFrontGate();
void processTimers();
void writeServoAngle(int pin, int channel, int angle);
void checkWiFiConnection();
void telnetPrintln(const String& msg);
void telnetPrint(const String& msg);
void processTelnetInput(); // Para manejar entrada Telnet
void processSerialInput(); // Para manejar entrada Serial USB (desde PC)
void processSerial2Input(); // Para manejar entrada Serial2 (desde Nano)

// Temporizador para mensajes de WiFi
unsigned long lastWiFiStatusPrint = 0;
const unsigned long WIFI_STATUS_INTERVAL = 10000; // 10 segundos
bool isWiFiReconnecting = false;
unsigned long lastTelnetInputTime = 0; // Para la lógica de la cola de mensajes Telnet
String telnetMessageQueue = ""; // Cola de mensajes para Telnet

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

  // Iniciar servidor Telnet
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  Serial.println("Servidor Telnet iniciado en puerto 23");
}

void loop() {
  // server.handleClient(); // Ya no se necesita
  processTimers();       // Procesa los temporizadores para la máquina de estados
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
      telnetClient.println("Comandos para ESP32: COLLECT, STOP (para recolección/compactación)");
      telnetClient.println("También puedes usar el Monitor Serial para enviar comandos.");
    } else {
      // Rechazar conexiones adicionales
      WiFiClient newClient = telnetServer.available();
      newClient.println("Ya hay un cliente Telnet conectado.");
      newClient.stop();
    }
  }

  processTelnetInput();  // Procesa comandos recibidos por Telnet
  processSerialInput();  // Procesa comandos recibidos por Serial USB
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
      telnetPrint("WiFi reconectando... Estado: ");
      telnetPrintln(String(WiFi.status())); 
      lastWiFiStatusPrint = currentMillis;
    }
  } else {
    digitalWrite(LED_BUILTIN, LOW); // LED apagado si conectado
    if (isWiFiReconnecting || currentMillis - lastWiFiStatusPrint >= WIFI_STATUS_INTERVAL) {
      telnetPrint("WiFi conectado. IP: ");
      telnetPrintln(WiFi.localIP().toString());
      telnetPrint("Intensidad de señal (RSSI): ");
      telnetPrintln(String(WiFi.RSSI()) + " dBm");
      isWiFiReconnecting = false;
      lastWiFiStatusPrint = currentMillis;
    }
  }
}

// Función unificada para ejecutar comandos, ya sea desde Telnet o Serial
void executeCommand(String command) {
  // Ignorar comandos de movimiento si estamos compactando
  if (systemState == COMPACTING) {
    telnetPrintln("AVISO: Comando '" + command + "' ignorado, sistema compactando.");
    return;
  }

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
    }
  }

  // Comandos que se envían al Nano
  if (command == "FORWARD") {
    moveForward();
    activateCollector(false); // Detener recolección al moverse
    telnetPrintln("Enviando F al Nano.");
  } else if (command == "LEFT") {
    turnLeft();
    activateCollector(false);
    telnetPrintln("Enviando L al Nano.");
  } else if (command == "RIGHT") {
    turnRight();
    activateCollector(false);
    telnetPrintln("Enviando R al Nano.");
  } else if (command == "STOP") {
    stopMotors();
    activateCollector(false);
    telnetPrintln("Enviando S al Nano y deteniendo recolección.");
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
    activateCollector(true);
    // Iniciar proceso de recolección no bloqueante
    systemState = COLLECTING;
    collectorTimerStart = millis();
    pendingCompaction = true; // Marcar que hay basura pendiente
    telnetPrintln("Iniciando recolección. Basura marcada como pendiente.");
  } else if (command.startsWith("E")) { // Comando de evasión (E1, E0)
    uint8_t enable = (command.length() > 1 && command[1] == '1') ? 1 : 0;
    Serial2.write('E');
    Serial2.write(enable);
    telnetPrintln("Comando Evasion (Nano): " + String(enable ? "ACTIVADA" : "DESACTIVADA") + " enviado al Nano");
  } else {
    // Si el comando no es reconocido, detener todo
    stopMotors();
    activateCollector(false);
    telnetPrintln("Comando desconocido: " + command + ". Deteniendo todo.");
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
  Serial2.write('L');
  Serial2.write(currentSpeed);
  telnetPrintln("Comando L" + String(currentSpeed) + " enviado al Nano.");
}

void turnRight() {
  Serial2.write('R');
  Serial2.write(currentSpeed);
  telnetPrintln("Comando R" + String(currentSpeed) + " enviado al Nano.");
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
  writeServoAngle(SERVO2_PIN, SERVO2_CHANNEL, 0);   // Ajustar ángulos según tu configuración física
  writeServoAngle(SERVO3_PIN, SERVO3_CHANNEL, 170);
  writeServoAngle(SERVO1_PIN, SERVO1_CHANNEL, 100);
  writeServoAngle(SERVO4_PIN, SERVO4_CHANNEL, 180);
  delay(100); // Pequeño delay para que los servos se muevan
  telnetPrintln("Compuerta frontal ABIERTA.");
}

void closeFrontGate() {
  telnetPrintln("Cerrando compuerta frontal...");
  writeServoAngle(SERVO2_PIN, SERVO2_CHANNEL, 100); // Ajustar ángulos según tu configuración física
  writeServoAngle(SERVO3_PIN, SERVO3_CHANNEL, 70);
  writeServoAngle(SERVO1_PIN, SERVO1_CHANNEL, 180);
  writeServoAngle(SERVO4_PIN, SERVO4_CHANNEL, 40);
  delay(100); // Pequeño delay para que los servos se muevan
  telnetPrintln("Compuerta frontal CERRADA.");
}

// Lógica de temporizador y compactador (máquina de estados)
void writeServoAngle(int pin, int channel, int angle) {
  // Calcular el ancho de pulso en microsegundos
  long pulseWidth = map(angle, 0, 180, SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);

  // Calcular el ciclo de trabajo para la resolución y frecuencia PWM
  uint32_t dutyCycle = (pulseWidth * (1 << SERVO_PWM_RESOLUTION)) / (1000000 / SERVO_PWM_FREQ);
  ledcWrite(channel, dutyCycle);
}

void processTimers() {
  static bool compactorStarted = false;
  static unsigned long compactorStepStart = 0;
  static int compactorStep = 0;

  unsigned long now = millis();

  // Proceso de recolección no bloqueante
  if (systemState == COLLECTING) {
    if (now - collectorTimerStart >= COLLECTOR_DURATION) {
      activateCollector(false);
      systemState = NORMAL; // Vuelve a NORMAL después de recolectar
      telnetPrintln("Proceso de recolección finalizado.");
    }
  }

  // Esperando para activar compactador en estado WAITING_FOR_STOP
  if (systemState == WAITING_FOR_STOP && compactTimerStart > 0) {
    if (now - compactTimerStart >= compactDelay) {
      // Iniciar compactador
      systemState = COMPACTING;
      compactorStarted = true;
      compactorStep = 0; // Reiniciar pasos del compactador
      compactorStepStart = now;
      closeFrontGate(); // Primer paso: cerrar compuerta
      compactTimerStart = 0; // Resetear temporizador de espera
      telnetPrintln("Iniciando proceso de compactación...");
    }
  }

  // Proceso de compactación
  if (systemState == COMPACTING && compactorStarted) {
    switch (compactorStep) {
      case 0: // Esperar a que cierre compuerta
        if (now - compactorStepStart > COMPACTOR_CLOSE_GATE_DELAY) {
          runCompactorMotor(true, COMPACTOR_SPEED); // Adelante (compactar)
          compactorStepStart = now;
          compactorStep = 1;
          telnetPrintln("Paso 1: Motor compactador adelante.");
        }
        break;
      case 1: // Motor adelante
        if (now - compactorStepStart > COMPACTOR_FORWARD_DURATION) {
          stopCompactorMotor();
          compactorStepStart = now;
          compactorStep = 2;
          telnetPrintln("Paso 2: Motor compactador detenido, esperando.");
        }
        break;
      case 2: // Esperar
        if (now - compactorStepStart > COMPACTOR_WAIT_DURATION) {
          runCompactorMotor(false, COMPACTOR_SPEED); // Atrás (volver a recolectar)
          compactorStepStart = now;
          compactorStep = 3;
          telnetPrintln("Paso 3: Motor compactador atrás.");
        }
        break;
      case 3: // Motor atrás
        if (now - compactorStepStart > COMPACTOR_BACKWARD_DURATION) {
          stopCompactorMotor();
          openFrontGate(); // Abrir compuerta
          compactorStepStart = now;
          compactorStep = 4;
          telnetPrintln("Paso 4: Motor compactador detenido, abriendo compuerta.");
        }
        break;
      case 4: // Esperar a que abra compuerta
        if (now - compactorStepStart > COMPACTOR_OPEN_GATE_DELAY) {
          // Fin del proceso
          systemState = NORMAL;
          compactorStarted = false;
          compactorStep = 0;
          compactTimerStart = 0;
          pendingCompaction = false; // Limpiar bandera tras compactar
          telnetPrintln("Proceso de compactación FINALIZADO. Sistema en NORMAL.");
        }
        break;
    }
  }
}

// Función para enviar mensajes a Serial y Telnet
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

// Procesa comandos recibidos por Telnet
void processTelnetInput() {
  if (telnetClient && telnetClient.connected() && telnetClient.available()) {
    lastTelnetInputTime = millis(); // Actualizar tiempo de última entrada Telnet
    String input = telnetClient.readStringUntil('\n');
    input.trim(); // Eliminar espacios en blanco y saltos de línea
    telnetPrintln("Telnet input: " + input); // Echo del comando recibido

    if (input.length() > 0) {
      char cmdChar = toupper(input[0]); // Convertir el primer carácter a mayúscula

      // Comandos para el Nano (movimiento y evasión)
      if (strchr("FBLRS", cmdChar)) { // Comandos de movimiento
        if (input.length() <= 1) {
          telnetClient.println("Comando Telnet inválido: velocidad requerida (ej: F200)");
        } else {
          int v = input.substring(1).toInt();
          if (v < 0 || v > 255) {
            telnetClient.println("Comando Telnet inválido: velocidad fuera de rango (0-255)");
          } else {
            uint8_t speedToNano = v;
            Serial2.write(cmdChar);
            Serial2.write(speedToNano);
            telnetClient.printf("Enviado al Nano: %c %d\n", cmdChar, speedToNano);
            Serial.printf("Comando Telnet: %c %d enviado al Nano\n", cmdChar, speedToNano);
            currentSpeed = speedToNano; // Actualizar la velocidad global
          }
        }
      } else { // Otros comandos para el ESP32 o Nano (E, COLLECT, STOP)
        executeCommand(input); // Usar la función unificada
      }
    }
  }
}

// Procesa comandos recibidos por Serial USB (desde PC)
void void processSerialInput() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim(); // Eliminar espacios en blanco y saltos de línea
    Serial.println("Serial input: " + input); // Echo del comando recibido

    if (input.length() > 0) {
      char cmdChar = toupper(input[0]); // Convertir el primer carácter a mayúscula

      // Comandos para el Nano (movimiento y evasión)
      if (strchr("FBLRS", cmdChar)) { // Comandos de movimiento
        if (input.length() <= 1) {
          Serial.println("Comando Serial inválido: velocidad requerida (ej: F200)");
        } else {
          int v = input.substring(1).toInt();
          if (v < 0 || v > 255) {
            Serial.println("Comando Serial inválido: velocidad fuera de rango (0-255)");
          } else {
            uint8_t speedToNano = v;
            Serial2.write(cmdChar);
            Serial2.write(speedToNano);
            Serial.printf("Enviado al Nano: %c %d\n", cmdChar, speedToNano);
            currentSpeed = speedToNano; // Actualizar la velocidad global
          }
        }
      } else { // Otros comandos para el ESP32 o Nano (E, COLLECT, STOP)
        executeCommand(input); // Usar la función unificada
      }
    }
  }
}

// Procesa datos recibidos del Nano a través de Serial2
void processSerial2Input() {
  static String nanoLine = ""; // Buffer para la línea del Nano
  while (Serial2.available()) {
    char c = Serial2.read();

    if (c == '\n' || c == '\r') {
      if (nanoLine.length() > 0) {
        String mensaje;
        if (nanoLine.startsWith("D:")) { // Si el Nano envía datos de depuración
          mensaje = "[Nano Debug] " + nanoLine + "\n";
        } else { // Otros mensajes del Nano
          mensaje = "[Nano] " + nanoLine + "\n";
        }
        Serial.print(mensaje); // Siempre imprimir en Serial USB
        telnetMessageQueue += mensaje; // Acumular para Telnet
        nanoLine = ""; // Limpiar el buffer
      }
    } else {
      nanoLine += c; // Construir la línea
    }
  }
}
