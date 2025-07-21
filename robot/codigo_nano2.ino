#include <SoftwareSerial.h> // Para comunicación Bluetooth
#include <Servo.h>          // Para control de servos

// --- CONFIGURACIÓN BLUETOOTH ---
#define BLUETOOTH_RX_PIN 2 // Pin RX del Arduino conectado al TX del módulo Bluetooth
#define BLUETOOTH_TX_PIN 4 // Pin TX del Arduino conectado al RX del módulo Bluetooth
SoftwareSerial bluetoothSerial(BLUETOOTH_RX_PIN, BLUETOOTH_TX_PIN); // RX, TX

// --- Pines Motores de Movilidad (Estos NO se usarán en este Nano, solo se reenvían comandos al Nano 2) ---
// Se mantienen las definiciones para claridad, pero las funciones 'moveForward', 'stopMotors', etc.
// NO controlarán pines locales de motor, sino que enviarán comandos al Serial.

// --- Pines Servos de Compuerta Frontal (con PWM) ---
// Estos pines se mantienen en este Nano.
#define SERVO1_PIN 6 // PWM
#define SERVO2_PIN 5 // PWM
#define SERVO3_PIN 3 // PWM

// Objetos Servo
Servo servo1;
Servo servo2;
Servo servo3;

// --- Pines Motores Adicionales (Gestionados por este Nano) ---
// COMPACTOR_MAIN y BRUSH son ON/OFF. COMPACTOR_ACTION tiene PWM.

// Motor principal del Compactador (ON/OFF - SIN PWM en ENA)
#define COMPACTOR_MAIN_IN1 A0 // Pin digital
#define COMPACTOR_MAIN_IN2 A1 // Pin digital
#define COMPACTOR_MAIN_ENA A4 // Pin digital (NO PWM)

// Motor DC para la acción del compactador (con PWM)
#define COMPACTOR_ACTION_IN1 A2
#define COMPACTOR_ACTION_IN2 A3
#define COMPACTOR_ACTION_ENA 11 // PWM

// Motor de Recolección (Cepillo) (ON/OFF - SIN PWM en ENB)
#define BRUSH_IN1 A5 // Pin digital
#define BRUSH_IN2 A6 // Pin digital
#define BRUSH_ENB A7 // Pin digital (NO PWM)

// --- Velocidades ---
// Estas velocidades se enviarán al Nano 2
#define SPEED_MOVE 200
#define SPEED_TURN 180

// Velocidades para motores controlados por este Nano
#define SPEED_COMPACTOR_MAIN 255
#define SPEED_BRUSH 200

// Parámetros para el nuevo motor DC del compactador
#define SPEED_COMPACTOR_ACTION 180
#define COMPACTOR_ACTION_DURATION 1500
#define COMPACTOR_DELAY_AFTER_MAIN 500

// --- Variables para la comunicación con Nano 2 ---
String nano2Line = ""; // Para acumular mensajes del Nano 2
unsigned long lastNano2Data = millis(); // Para saber cuándo fue el último dato

// --- Prototipos de funciones ---
void setMotor(int in1, int in2, int ena, bool forward, uint8_t vel); // Función auxiliar local
void sendCommandToNano2(char cmd, uint8_t vel); // Nuevo: para enviar comandos al Nano 2
void sendEnableAvoidanceToNano2(bool enable); // Nuevo: para enviar estado de evasión

// Funciones de control de movimiento (ahora envían comandos al Nano 2)
void moveForward(uint8_t vel);
void moveBackward(uint8_t vel);
void turnLeft(uint8_t vel);
void turnRight(uint8_t vel);
void stopMotors();

// Funciones para motores adicionales (se quedan en este Nano)
void activateCompactorMain(uint8_t vel);
void stopCompactorMain();
void activateCompactorAction(uint8_t vel);
void stopCompactorAction();
void activateBrush(uint8_t vel);
void stopBrush();
void activateCompactorSequence();

// Funciones para Servos (se quedan en este Nano)
void openFrontGate();
void closeFrontGate();

void processDabbleCommand(char cmd);


void setup() {
  Serial.begin(9600);      // ¡CUIDADO! Este Serial se usará para comunicar con el Nano 2.
                           // Si usas el Monitor Serial del IDE, desconecta el Nano 2 primero.
  bluetoothSerial.begin(9600); // Inicia comunicación con el módulo Bluetooth

  // Configurar pines de los motores adicionales (Compactor y Brush)
  pinMode(COMPACTOR_MAIN_IN1, OUTPUT);
  pinMode(COMPACTOR_MAIN_IN2, OUTPUT);
  pinMode(COMPACTOR_MAIN_ENA, OUTPUT);

  pinMode(COMPACTOR_ACTION_IN1, OUTPUT);
  pinMode(COMPACTOR_ACTION_IN2, OUTPUT);
  pinMode(COMPACTOR_ACTION_ENA, OUTPUT);

  pinMode(BRUSH_IN1, OUTPUT);
  pinMode(BRUSH_IN2, OUTPUT);
  pinMode(BRUSH_ENB, OUTPUT);

  // Asegurarse de que los motores adicionales estén apagados al inicio
  stopBrush();
  stopCompactorMain();
  stopCompactorAction();

  // Configurar servos
  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  servo3.attach(SERVO3_PIN);

  openFrontGate(); // Abrir compuerta al inicio

  Serial.println("Nano Principal iniciado. Esperando comandos Bluetooth.");
  Serial.println("Comunicación con Nano 2 en D0/D1.");
}

void loop() {
  // Leer comando desde Bluetooth (Gamepad Dabble)
  if (bluetoothSerial.available()) {
    char cmd = bluetoothSerial.read();
    Serial.print("Comando Bluetooth recibido: ");
    Serial.println(cmd);
    processDabbleCommand(cmd);
  }

  // Leer datos del Nano 2 (como la distancia del ultrasónico)
  while (Serial.available()) {
    char c = Serial.read();
    lastNano2Data = millis();
    if (c == '\n' || c == '\r') {
      if (nano2Line.length() > 0) {
        // Aquí puedes procesar el mensaje del Nano 2
        Serial.print("[Nano 2] "); // Prefijo para identificar mensajes del Nano 2
        Serial.println(nano2Line);
        nano2Line = "";
      }
    } else {
      nano2Line += c;
    }
  }
}

// --- Rutinas principales de control de movimiento ---

void processDabbleCommand(char cmd) {
  switch (cmd) {
    case 'F': // Joystick Arriba
      moveForward(SPEED_MOVE);
      break;
    case 'B': // Joystick Abajo
      moveBackward(SPEED_MOVE);
      break;
    case 'L': // Joystick Izquierda
      turnLeft(SPEED_TURN);
      break;
    case 'R': // Joystick Derecha
      turnRight(SPEED_TURN);
      break;
    case 'S': // Joystick Centrado o botón liberado (Dabble envía 'S' al soltar)
      stopMotors();
      stopBrush();
      stopCompactorMain();
      stopCompactorAction();
      break;
    case '1': // Botón Cuadrado/A (Asignado a Compactador)
      activateCompactorSequence(); // ESTO ES BLOQUEANTE
      break;
    case '2': // Botón Triángulo/B (Asignado a Cepillo)
      activateBrush(SPEED_BRUSH);
      break;
    case '3': // Botón Círculo/X (Asignado a Abrir Compuerta)
      openFrontGate();
      break;
    case '4': // Botón Cruz/Y (Asignado a Cerrar Compuerta)
      closeFrontGate();
      break;
    case 'E': // Si Dabble tuviera un botón para evadir (ej. enviar 'E')
      // Esto es solo un ejemplo, tendrías que asignar un botón en Dabble
      // para enviar 'E1' (activar) o 'E0' (desactivar)
      // Por simplicidad, aquí solo se activa la evasión. Si necesitas alternar,
      // necesitarás más lógica o botones en Dabble.
      sendEnableAvoidanceToNano2(true); // O false, dependiendo del botón
      break;
    default:
      // Si recibimos cualquier otro caracter, asumimos que es una señal para detenerse
      // o un caracter no reconocido, y detenemos todo por seguridad.
      stopMotors(); // Detiene motores del Nano 2
      stopBrush(); // Detiene motores locales
      stopCompactorMain(); // Detiene motores locales
      stopCompactorAction(); // Detiene motores locales
      break;
  }
}

// --- Funciones para enviar comandos al Nano 2 ---
// Sigue el protocolo: 1 byte de comando (char), 1 byte de velocidad (uint8_t)
void sendCommandToNano2(char cmd, uint8_t vel) {
  Serial.write(cmd);
  Serial.write(vel);
}

// Envía comando para activar/desactivar evasión de obstáculos
void sendEnableAvoidanceToNano2(bool enable) {
  Serial.write('E');
  Serial.write(enable ? 1 : 0);
  Serial.print("Evasion de obstaculos enviada al Nano 2: ");
  Serial.println(enable ? "ACTIVADA" : "DESACTIVADA");
}

// --- Rutinas de control de movimiento que envían comandos al Nano 2 ---
void moveForward(uint8_t vel) {
  sendCommandToNano2('F', vel);
}

void moveBackward(uint8_t vel) {
  sendCommandToNano2('B', vel);
}

void turnLeft(uint8_t vel) {
  sendCommandToNano2('L', vel);
}

void turnRight(uint8_2_t vel) {
  sendCommandToNano2('R', vel);
}

void stopMotors() {
  sendCommandToNano2('S', 0); // La velocidad 0 se ignora para 'S', pero se envía para consistencia
}

// *** IMPORTANTE: Esta función se modificó para manejar pines ENA que no son PWM. ***
// Para pines ENA que no son PWM, 'vel' solo determinará ON (HIGH) u OFF (LOW).
// Esta función es solo para los motores controlados LOCALMENTE (Compactador y Cepillo)
void setMotor(int in1, int in2, int ena, bool forward, uint8_t vel) {
  digitalWrite(in1, forward ? HIGH : LOW);
  digitalWrite(in2, forward ? LOW : HIGH);

  // Comprobar si el pin 'ena' es un pin PWM hardware en el Nano
  // Los pines PWM son: D3, D5, D6, D9, D10, D11
  if (ena == 3 || ena == 5 || ena == 6 || ena == 9 || ena == 10 || ena == 11) {
    analogWrite(ena, vel); // Usar PWM si el pin lo soporta
  } else {
    // Si el pin no es PWM, se usa como ON/OFF digital
    digitalWrite(ena, (vel > 0) ? HIGH : LOW);
  }
}

// --- Funciones para Motores Adicionales (gestionadas por este Arduino Nano) ---
void activateCompactorMain(uint8_t vel) {
  setMotor(COMPACTOR_MAIN_IN1, COMPACTOR_MAIN_IN2, COMPACTOR_MAIN_ENA, true, vel);
  Serial.println("Compactador Principal Activado");
}

void stopCompactorMain() {
  setMotor(COMPACTOR_MAIN_IN1, COMPACTOR_MAIN_IN2, COMPACTOR_MAIN_ENA, true, 0); // Velocidad 0 para detener
  Serial.println("Compactador Principal Detenido");
}

void activateCompactorAction(uint8_2_t vel) {
  setMotor(COMPACTOR_ACTION_IN1, COMPACTOR_ACTION_IN2, COMPACTOR_ACTION_ENA, true, vel); // Asume una dirección
  Serial.println("Compactador Acción Activado");
}

void stopCompactorAction() {
  setMotor(COMPACTOR_ACTION_IN1, COMPACTOR_ACTION_IN2, COMPACTOR_ACTION_ENA, true, 0); // Velocidad 0 para detener
  Serial.println("Compactador Acción Detenido");
}

void activateBrush(uint8_t vel) {
  setMotor(BRUSH_IN1, BRUSH_IN2, BRUSH_ENB, true, vel);
  Serial.println("Cepillo Activado");
}

void stopBrush() {
  setMotor(BRUSH_IN1, BRUSH_IN2, BRUSH_ENB, true, 0); // Velocidad 0 para detener
  Serial.println("Cepillo Detenido");
}

// Función de secuencia de compactación (bloqueante)
void activateCompactorSequence() {
  activateCompactorMain(SPEED_COMPACTOR_MAIN);
  delay(COMPACTOR_DELAY_AFTER_MAIN);
  activateCompactorAction(SPEED_COMPACTOR_ACTION);
  delay(COMPACTOR_ACTION_DURATION);
  stopCompactorAction();
  stopCompactorMain();
  Serial.println("Secuencia de Compactación Completa");
}

// --- Funciones de control de Servos (gestionadas por este Arduino Nano) ---
void openFrontGate() {
  servo1.write(90);
  servo2.write(90);
  servo3.write(90);
  Serial.println("Compuerta Frontal Abierta");
}

void closeFrontGate() {
  servo1.write(0);
  servo2.write(0);
  servo3.write(0);
  Serial.println("Compuerta Frontal Cerrada");
}