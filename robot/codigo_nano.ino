// --- Pines Motores ---
#define M1_IN1 7
#define M1_IN2 8
#define M1_ENA 9

#define M2_IN3 11
#define M2_IN4 12
#define M2_ENB 10

// --- Sensor Ultrasónico HC-SR04 ---
#define TRIG_PIN A0
#define ECHO_PIN A1

// --- Parámetros ---
#define OBSTACLE_DISTANCE_CM 25     // Distancia de detección de obstáculo
#define OBSTACLE_AVOID_TIME 600     // ms para avanzar/retroceder/girar en evitado

// --- Variables ---
bool obstacle_avoidance_enabled = false; // Activado por defecto

unsigned long lastDistanceSent = 0;
const unsigned long DISTANCE_SEND_INTERVAL = 200; // ms

void setup() {
  // Motores
  pinMode(M1_IN1, OUTPUT); pinMode(M1_IN2, OUTPUT); pinMode(M1_ENA, OUTPUT);
  pinMode(M2_IN3, OUTPUT); pinMode(M2_IN4, OUTPUT); pinMode(M2_ENB, OUTPUT);

  // Sensor ultrasónico
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Serial.begin(9600); // Comunicación con ESP32
  stopMotors();
}

void loop() {
  // Checa obstáculo si la función está activada
  if (obstacle_avoidance_enabled && detectObstacle(OBSTACLE_DISTANCE_CM)) {
    avoidObstacle();
    return;
  }

  // Enviar distancia periódicamente al ESP32
  unsigned long now = millis();
  if (now - lastDistanceSent >= DISTANCE_SEND_INTERVAL) {
    float dist = measureDistance();
    Serial.print("D:");
    Serial.println(dist, 1); // Un decimal de precisión
    lastDistanceSent = now;
  }

  // Leer comando desde ESP32
  if (Serial.available() >= 2) {
    char cmd = Serial.read();
    uint8_t speed = Serial.read();

    if (cmd == 'E') {
      obstacle_avoidance_enabled = speed ? true : false;
      Serial.print("Evasion de obstaculos: ");
      Serial.println(obstacle_avoidance_enabled ? "ACTIVADA" : "DESACTIVADA");
      return;
    }
    
    if (speed < 0 || speed > 255) {
      Serial.println("Velocidad no válida. Debe ser entre 0 y 255.");
      return;
    }

    processCommand(cmd, speed);
    if (cmd == 'S') {
      Serial.println("Motores detenidos.");
    } else {
      Serial.print("Comando recibido: ");
      Serial.print(cmd);
      Serial.print(" con velocidad: ");
      Serial.println(speed);
    }
  }
}

// --- Rutinas principales ---

void processCommand(char cmd, uint8_t vel) {
  switch (cmd) {
    case 'F': moveForward(vel); break;
    case 'B': moveBackward(vel); break;
    case 'L': turnLeft(vel); break;
    case 'R': turnRight(vel); break;
    case 'S': stopMotors(); break;
    default:  stopMotors();
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
  setMotor(M1_IN1, M1_IN2, M1_ENA, false, vel);
  setMotor(M2_IN3, M2_IN4, M2_ENB, true, vel);
}

void turnRight(uint8_t vel) {
  setMotor(M1_IN1, M1_IN2, M1_ENA, true, vel);
  setMotor(M2_IN3, M2_IN4, M2_ENB, false, vel);
}

void stopMotors() {
  setMotor(M1_IN1, M1_IN2, M1_ENA, true, 0);
  setMotor(M2_IN3, M2_IN4, M2_ENB, true, 0);
}

// --- Motor helper ---
void setMotor(int in1, int in2, int ena, bool forward, uint8_t vel) {
  digitalWrite(in1, forward ? HIGH : LOW);
  digitalWrite(in2, forward ? LOW : HIGH);
  analogWrite(ena, vel);
}

// --- Sensor ultrasónico ---
bool detectObstacle(float limit_cm) {
  float distance_cm = measureDistance();
  return (distance_cm > 0 && distance_cm <= limit_cm);
}

// Nueva función para medir distancia
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

// --- Algoritmo de esquivar obstáculo ---
void avoidObstacle() {
  // 1. Detenerse
  stopMotors();
  delay(1250);

  // 2. Retroceder
  moveBackward(90);
  delay(OBSTACLE_AVOID_TIME);

  // 3. Girar derecha
  turnRight(95);
  delay(OBSTACLE_AVOID_TIME);

  // 4. Avanzar
  moveForward(80);
  delay(OBSTACLE_AVOID_TIME);

  // 5. Detenerse y esperar
  stopMotors();
  delay(200);
}