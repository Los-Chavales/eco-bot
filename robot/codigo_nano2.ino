#include <SoftwareSerial.h> // Para comunicación Bluetooth
#include <Servo.h>          // Para control de servos

// --- CONFIGURACIÓN BLUETOOTH ---
#define BLUETOOTH_RX_PIN 2 // Pin RX del Arduino conectado al TX del módulo Bluetooth
#define BLUETOOTH_TX_PIN 4 // Pin TX del Arduino conectado al RX del módulo Bluetooth (¡CAMBIADO A D4 para liberar D3!)
SoftwareSerial bluetoothSerial(BLUETOOTH_RX_PIN, BLUETOOTH_TX_PIN); // RX, TX

// --- Pines Motores de Movilidad (Arduino Nano - con PWM) ---
// Asume un driver L298N o similar para 2 motores de movilidad
#define M1_IN1 7
#define M1_IN2 8
#define M1_ENA 9  // PWM (OK)

#define M2_IN3 12
#define M2_IN4 13 // Pin digital 13 (usado también por M2_IN4)
#define M2_ENB 10 // PWM (OK)

// --- Pines Servos de Compuerta Frontal (con PWM) ---
// Estos pines se mantienen como solicitaste.
#define SERVO1_PIN 6 // PWM (OK)
#define SERVO2_PIN 5 // PWM (OK)
#define SERVO3_PIN 3 // PWM (OK, D3 ahora está libre ya que Bluetooth TX se movió a D4)

// Objetos Servo
Servo servo1;
Servo servo2;
Servo servo3;

// --- Pines Motores Adicionales (Arduino Nano - Reasignados) ---
// **IMPORTANTE:** Para el Nano, estos motores operarán con limitaciones de PWM.
// COMPACTOR_MAIN y BRUSH son ON/OFF. COMPACTOR_ACTION tiene PWM.

// Motor principal del Compactador (ON/OFF - SIN PWM en ENA)
// ¡Pines reasignados para evitar conflictos y no usar D0/D1!
#define COMPACTOR_MAIN_IN1 A0 // Nuevo pin digital
#define COMPACTOR_MAIN_IN2 A1 // Nuevo pin digital
#define COMPACTOR_MAIN_ENA A4 // Nuevo pin digital (NO PWM - se usará digitalWrite)

// Motor DC para la acción del compactador (con PWM)
#define COMPACTOR_ACTION_IN1 A2
#define COMPACTOR_ACTION_IN2 A3
#define COMPACTOR_ACTION_ENA 11 // PWM (OK - utiliza el último pin PWM disponible)

// Motor de Recolección (Cepillo) (ON/OFF - SIN PWM en ENB)
// ¡Pines reasignados para evitar conflictos y no usar D0/D1!
#define BRUSH_IN1 A5 // Pin digital
#define BRUSH_IN2 A6 // Nuevo pin digital
#define BRUSH_ENB A7 // Nuevo pin digital (NO PWM - se usará digitalWrite)

// --- Velocidades ---
#define SPEED_MOVE 200
#define SPEED_TURN 180
#define SPEED_COMPACTOR_MAIN 255 // Velocidad máxima para compactador principal (si es ON/OFF)
#define SPEED_BRUSH 200          // Velocidad máxima para cepillo (si es ON/OFF)

// Parámetros para el nuevo motor DC del compactador
#define SPEED_COMPACTOR_ACTION 180 // Velocidad para el motor de acción del compactador (con PWM)
#define COMPACTOR_ACTION_DURATION 1500 // Duración del movimiento del motor de acción en ms
#define COMPACTOR_DELAY_AFTER_MAIN 500 // Retraso entre el motor principal y el de acción en ms

// --- Prototipos de funciones ---
void setMotor(int in1, int in2, int ena, bool forward, uint8_t vel);
void stopMotors();
void moveForward(uint8_t vel);
void moveBackward(uint8_t vel);
void turnLeft(uint8_t vel);
void turnRight(uint8_t vel);

void activateCompactorMain(uint8_t vel);
void stopCompactorMain();
void activateCompactorAction(uint8_t vel);
void stopCompactorAction();
void activateBrush(uint8_t vel);
void stopBrush();
void activateCompactorSequence();

void openFrontGate();
void closeFrontGate();

void processDabbleCommand(char cmd);

void setup() {
  Serial.begin(9600);      // Para depuración en el monitor serial de Arduino IDE
  bluetoothSerial.begin(9600); // Inicia comunicación con el módulo Bluetooth

  // Configurar pines de los motores de movilidad
  pinMode(M1_IN1, OUTPUT); pinMode(M1_IN2, OUTPUT); pinMode(M1_ENA, OUTPUT);
  pinMode(M2_IN3, OUTPUT); pinMode(M2_IN4, OUTPUT); pinMode(M2_ENB, OUTPUT);

  // Configurar pines de los motores adicionales
  pinMode(COMPACTOR_MAIN_IN1, OUTPUT);
  pinMode(COMPACTOR_MAIN_IN2, OUTPUT);
  pinMode(COMPACTOR_MAIN_ENA, OUTPUT); // Digital, no PWM

  pinMode(COMPACTOR_ACTION_IN1, OUTPUT);
  pinMode(COMPACTOR_ACTION_IN2, OUTPUT);
  pinMode(COMPACTOR_ACTION_ENA, OUTPUT); // PWM

  pinMode(BRUSH_IN1, OUTPUT);
  pinMode(BRUSH_IN2, OUTPUT);
  pinMode(BRUSH_ENB, OUTPUT); // Digital, no PWM

  // Asegurarse de que todos los motores estén apagados al inicio
  stopMotors();
  stopBrush();
  stopCompactorMain();
  stopCompactorAction();

  // Configurar servos
  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  servo3.attach(SERVO3_PIN);

  openFrontGate(); // Abrir compuerta al inicio

  Serial.println("Arduino Nano iniciado y listo para comandos Bluetooth.");
  Serial.println("Conecta la app Dabble (Gamepad) via Bluetooth.");
}

void loop() {
  // Leer comando desde Bluetooth
  if (bluetoothSerial.available()) {
    char cmd = bluetoothSerial.read();
    Serial.print("Comando Bluetooth recibido: ");
    Serial.println(cmd);
    processDabbleCommand(cmd);
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
    default:
      // Si recibimos cualquier otro caracter, asumimos que es una señal para detenerse
      // o un caracter no reconocido, y detenemos todo por seguridad.
      stopMotors();
      stopBrush();
      stopCompactorMain();
      stopCompactorAction();
      break;
  }
}

// *** IMPORTANTE: Esta función se modificó para manejar pines ENA que no son PWM. ***
// Para pines ENA que no son PWM, 'vel' solo determinará ON (HIGH) u OFF (LOW).
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

void moveForward(uint8_t vel) {
  setMotor(M1_IN1, M1_IN2, M1_ENA, true, vel);
  setMotor(M2_IN3, M2_IN4, M2_ENB, true, vel);
}

void moveBackward(uint8_t vel) {
  setMotor(M1_IN1, M1_IN2, M1_ENA, false, vel);
  setMotor(M2_IN3, M2_IN4, M2_ENB, false, vel);
}

void turnLeft(uint8_t vel) {
  setMotor(M1_IN1, M1_IN2, M1_ENA, false, vel); // Motor 1 hacia atrás
  setMotor(M2_IN3, M2_IN4, M2_ENB, true, vel);  // Motor 2 hacia adelante
}

void turnRight(uint8_t vel) {
  setMotor(M1_IN1, M1_IN2, M1_ENA, true, vel);  // Motor 1 hacia adelante
  setMotor(M2_IN3, M2_IN4, M2_ENB, false, vel); // Motor 2 hacia atrás
}

void stopMotors() {
  setMotor(M1_IN1, M1_IN2, M1_ENA, true, 0); // Velocidad 0 para detener
  setMotor(M2_IN3, M2_IN4, M2_ENB, true, 0); // Velocidad 0 para detener
}

// --- Funciones para Motores Adicionales (gestionadas por Arduino Nano) ---
void activateCompactorMain(uint8_t vel) {
  setMotor(COMPACTOR_MAIN_IN1, COMPACTOR_MAIN_IN2, COMPACTOR_MAIN_ENA, true, vel);
  Serial.println("Compactador Principal Activado");
}

void stopCompactorMain() {
  setMotor(COMPACTOR_MAIN_IN1, COMPACTOR_MAIN_IN2, COMPACTOR_MAIN_ENA, true, 0); // Velocidad 0 para detener
  Serial.println("Compactador Principal Detenido");
}

void activateCompactorAction(uint8_t vel) {
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
  // 1. Activa el motor principal del compactador
  activateCompactorMain(SPEED_COMPACTOR_MAIN); // Este motor ahora es ON/OFF
  delay(COMPACTOR_DELAY_AFTER_MAIN); // Espera un poco para que el motor principal actúe

  // 2. Activa el motor de acción (simulando el movimiento del servo)
  activateCompactorAction(SPEED_COMPACTOR_ACTION); // Este motor sí tiene PWM
  delay(COMPACTOR_ACTION_DURATION); // Mantiene el motor de acción girando por la duración
  stopCompactorAction();            // Detiene el motor de acción

  // 3. Detiene el motor principal del compactador
  stopCompactorMain(); // Este motor ahora es ON/OFF
  Serial.println("Secuencia de Compactación Completa");
}

// --- Funciones de control de Servos (gestionadas por Arduino Nano) ---
void openFrontGate() {
  servo1.write(90); // Abre a 90 grados
  servo2.write(90);
  servo3.write(90);
  Serial.println("Compuerta Frontal Abierta");
}

void closeFrontGate() {
  servo1.write(0); // Cierra a 0 grados
  servo2.write(0);
  servo3.write(0);
  Serial.println("Compuerta Frontal Cerrada");
}