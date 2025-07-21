#include <SoftwareSerial.h> // Para comunicación Bluetooth
#include <Servo.h>          // Para control de servos

// --- CONFIGURACIÓN BLUETOOTH ---
#define BLUETOOTH_RX_PIN 2 // Pin RX del Arduino conectado al TX del módulo Bluetooth
#define BLUETOOTH_TX_PIN 3 // Pin TX del Arduino conectado al RX del módulo Bluetooth
SoftwareSerial bluetoothSerial(BLUETOOTH_RX_PIN, BLUETOOTH_TX_PIN); // RX, TX

// --- Pines Motores de Movilidad (Arduino Nano) ---
// Asume un driver L298N o similar para 2 motores de movilidad
#define M1_IN1 7
#define M1_IN2 8
#define M1_ENA 9  // PWM

#define M2_IN3 11
#define M2_IN4 12
#define M2_ENB 10 // PWM

// --- Pines Motores Adicionales (Arduino Nano) ---
// **IMPORTANTE:** Estos pines asumen que tienes drivers adicionales para estos motores.
// Un solo L298N solo maneja 2 motores DC.
// Si solo tienes un L298N, necesitarás reevaluar cómo controlar estos motores.
// Para este ejemplo, se asume que cada motor tiene su propio control (ej. otro L298N, o transistores/reles).

// Motor principal del Compactador
#define COMPACTOR_MAIN_IN1 4
#define COMPACTOR_MAIN_IN2 5
#define COMPACTOR_MAIN_ENA 6 // PWM

// Motor DC para la acción del compactador (simula el movimiento del servo)
#define COMPACTOR_ACTION_IN1 A2 // Pines para el nuevo motor DC
#define COMPACTOR_ACTION_IN2 A3
#define COMPACTOR_ACTION_ENA A4 // PWM para el nuevo motor DC

// Motor de Recolección (Cepillo)
#define BRUSH_IN1 A5
#define BRUSH_IN2 13 // Pin digital 13
#define BRUSH_ENB 5  // PWM (¡Cuidado! Pin 5 ya usado para COMPACTOR_MAIN_IN2. Reasignar si es un problema)
                     // **NOTA:** Si el pin 5 está en uso por COMPACTOR_MAIN_IN2, debes reasignar BRUSH_ENB a otro pin PWM (ej. 11 o 10 si no se usan para M2_ENB).
                     // Para este ejemplo, asumiré que puedes usar el pin 5 como PWM si COMPACTOR_MAIN_IN2 es solo digital.
                     // Si el pin 5 es compartido y ambos necesitan PWM, esto causará un conflicto.
                     // He reasignado BRUSH_ENB a 11 para evitar conflictos con COMPACTOR_MAIN_IN2.

#undef BRUSH_ENB // Eliminar la definición anterior para redefinir
#define BRUSH_ENB 11 // Reasignado a un pin PWM disponible (si M2_ENB no lo usa)
                     // Si M2_ENB usa el pin 10, y M1_ENA usa el 9, el pin 11 es una buena opción para PWM.

// --- Pines de Servos de Compuerta Frontal ---
#define SERVO1_PIN 12 // Pin PWM disponible
#define SERVO2_PIN 4  // Pin PWM disponible (¡Cuidado! Pin 4 ya usado para COMPACTOR_MAIN_IN1. Reasignar si es un problema)
#define SERVO3_PIN 5  // Pin PWM disponible (¡Cuidado! Pin 5 ya usado para COMPACTOR_MAIN_IN2. Reasignar si es un problema)

// **NOTA IMPORTANTE SOBRE PINES:**
// Los pines 4, 5, 6, 9, 10, 11, 12 son pines PWM en Arduino Nano.
// He intentado reasignar para evitar conflictos, pero DEBES verificar tus conexiones físicas y la disponibilidad de pines PWM.
// Si un pin se usa como entrada/salida digital para un motor y como PWM para un servo, habrá un conflicto.
// Recomiendo usar pines PWM dedicados para los servos (3, 5, 6, 9, 10, 11).
// Reasignemos los servos a pines PWM más seguros:
#undef SERVO1_PIN
#undef SERVO2_PIN
#undef SERVO3_PIN
#define SERVO1_PIN 6 // PWM
#define SERVO2_PIN 5 // PWM
#define SERVO3_PIN 3 // PWM

// Objetos Servo
Servo servo1;
Servo servo2;
Servo servo3;

// --- Sensor Ultrasónico HC-SR04 ---
#define TRIG_PIN A0
#define ECHO_PIN A1

// --- Parámetros ---
#define OBSTACLE_DISTANCE_CM 25    // Distancia de detección de obstáculo
#define OBSTACLE_AVOID_TIME 600    // ms para avanzar/retroceder/girar en evitado

// --- Velocidades ---
#define SPEED_MOVE 200
#define SPEED_TURN 180
#define SPEED_COMPACTOR_MAIN 255
#define SPEED_BRUSH 200

// Parámetros para el nuevo motor DC del compactador
#define SPEED_COMPACTOR_ACTION 180 // Velocidad para el motor de acción del compactador
#define COMPACTOR_ACTION_DURATION 1500 // Duración del movimiento del motor de acción en ms
#define COMPACTOR_DELAY_AFTER_MAIN 500 // Retraso entre el motor principal y el de acción en ms

// --- VARIABLES GLOBALES ---
bool obstacle_avoidance_enabled = true; // Activado por defecto
unsigned long lastDistanceSent = 0;
const unsigned long DISTANCE_SEND_INTERVAL = 500; // ms

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

float measureDistance();
bool detectObstacle(float limit_cm);
void avoidObstacle();

void processDabbleCommand(char cmd);

void setup() {
  Serial.begin(9600); // Para depuración en el monitor serial de Arduino IDE
  bluetoothSerial.begin(9600); // Inicia comunicación con el módulo Bluetooth

  // Configurar pines de los motores de movilidad
  pinMode(M1_IN1, OUTPUT); pinMode(M1_IN2, OUTPUT); pinMode(M1_ENA, OUTPUT);
  pinMode(M2_IN3, OUTPUT); pinMode(M2_IN4, OUTPUT); pinMode(M2_ENB, OUTPUT);

  // Configurar pines de los motores adicionales
  pinMode(COMPACTOR_MAIN_IN1, OUTPUT);
  pinMode(COMPACTOR_MAIN_IN2, OUTPUT);
  pinMode(COMPACTOR_MAIN_ENA, OUTPUT);

  pinMode(COMPACTOR_ACTION_IN1, OUTPUT);
  pinMode(COMPACTOR_ACTION_IN2, OUTPUT);
  pinMode(COMPACTOR_ACTION_ENA, OUTPUT);

  pinMode(BRUSH_IN1, OUTPUT);
  pinMode(BRUSH_IN2, OUTPUT);
  pinMode(BRUSH_ENB, OUTPUT);

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

  // Sensor ultrasónico
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Serial.println("Arduino Nano iniciado y listo para comandos Bluetooth.");
  Serial.println("Conecta la app Dabble (Gamepad) via Bluetooth.");
}

void loop() {
  // Checa obstáculo si la función está activada
  if (obstacle_avoidance_enabled && detectObstacle(OBSTACLE_DISTANCE_CM)) {
    avoidObstacle();
    // Después de evitar, detenemos los motores para evitar que siga intentando moverse
    // y para dar tiempo a que el usuario reaccione.
    stopMotors();
    return;
  }

  // Enviar distancia periódicamente (opcional, para depuración o si tienes una interfaz que la muestre)
  unsigned long now = millis();
  if (now - lastDistanceSent >= DISTANCE_SEND_INTERVAL) {
    float dist = measureDistance();
    Serial.print("Distancia: ");
    Serial.print(dist, 1); // Un decimal de precisión
    Serial.println(" cm");
    // Si quisieras enviar esto a Dabble, tendrías que usar un módulo de texto en Dabble
    // y enviar algo como: bluetoothSerial.print("D:"); bluetoothSerial.println(dist);
    lastDistanceSent = now;
  }

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
    case 'E': // Si quisieras un botón para activar/desactivar evasión de obstáculos
      obstacle_avoidance_enabled = !obstacle_avoidance_enabled;
      Serial.print("Evasion de obstaculos: ");
      Serial.println(obstacle_avoidance_enabled ? "ACTIVADA" : "DESACTIVADA");
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

void setMotor(int in1, int in2, int ena, bool forward, uint8_t vel) {
  digitalWrite(in1, forward ? HIGH : LOW);
  digitalWrite(in2, forward ? LOW : HIGH);
  analogWrite(ena, vel);
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
  setMotor(M1_IN1, M1_IN2, M1_ENA, true, 0);
  setMotor(M2_IN3, M2_IN4, M2_ENB, true, 0);
}

// --- Funciones para Motores Adicionales (gestionadas por Arduino Nano) ---
void activateCompactorMain(uint8_t vel) {
  setMotor(COMPACTOR_MAIN_IN1, COMPACTOR_MAIN_IN2, COMPACTOR_MAIN_ENA, true, vel);
  Serial.println("Compactador Principal Activado");
}

void stopCompactorMain() {
  setMotor(COMPACTOR_MAIN_IN1, COMPACTOR_MAIN_IN2, COMPACTOR_MAIN_ENA, true, 0);
  Serial.println("Compactador Principal Detenido");
}

void activateCompactorAction(uint8_t vel) {
  setMotor(COMPACTOR_ACTION_IN1, COMPACTOR_ACTION_IN2, COMPACTOR_ACTION_ENA, true, vel); // Asume una dirección
  Serial.println("Compactador Acción Activado");
}

void stopCompactorAction() {
  setMotor(COMPACTOR_ACTION_IN1, COMPACTOR_ACTION_IN2, COMPACTOR_ACTION_ENA, true, 0);
  Serial.println("Compactador Acción Detenido");
}

void activateBrush(uint8_t vel) {
  setMotor(BRUSH_IN1, BRUSH_IN2, BRUSH_ENB, true, vel);
  Serial.println("Cepillo Activado");
}

void stopBrush() {
  setMotor(BRUSH_IN1, BRUSH_IN2, BRUSH_ENB, true, 0); // Detener
  Serial.println("Cepillo Detenido");
}

// Función de secuencia de compactación (bloqueante)
void activateCompactorSequence() {
  // 1. Activa el motor principal del compactador
  activateCompactorMain(SPEED_COMPACTOR_MAIN);
  delay(COMPACTOR_DELAY_AFTER_MAIN); // Espera un poco para que el motor principal actúe

  // 2. Activa el motor de acción (simulando el movimiento del servo)
  activateCompactorAction(SPEED_COMPACTOR_ACTION);
  delay(COMPACTOR_ACTION_DURATION); // Mantiene el motor de acción girando por la duración
  stopCompactorAction();            // Detiene el motor de acción

  // 3. Detiene el motor principal del compactador
  stopCompactorMain();
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

// --- Sensor ultrasónico ---
float measureDistance() {
  long duration;
  float distance_cm;

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  duration = pulseIn(ECHO_PIN, HIGH, 25000); // Timeout ~4.3m
  distance_cm = duration * 0.034 / 2;

  return distance_cm;
}

bool detectObstacle(float limit_cm) {
  float distance_cm = measureDistance();
  return (distance_cm > 0 && distance_cm <= limit_cm);
}

// --- Algoritmo de esquivar obstáculo ---
void avoidObstacle() {
  Serial.println("Obstaculo detectado! Iniciando evasion...");
  // 1. Detenerse
  stopMotors();
  delay(1250);

  // 2. Retroceder
  moveBackward(80);
  delay(OBSTACLE_AVOID_TIME);

  // 3. Girar derecha
  turnRight(85);
  delay(OBSTACLE_AVOID_TIME);

  // 4. Avanzar
  moveForward(65);
  delay(OBSTACLE_AVOID_TIME);

  // 5. Detenerse
  stopMotors();
  Serial.println("Evasion de obstaculos completada.");
}