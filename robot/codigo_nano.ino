// --- Pines Motores de Movilidad ---
#define M1_IN1 7
#define M1_IN2 8
#define M1_ENA 9  // PWM

#define M2_IN3 11
#define M2_IN4 12
#define M2_ENB 10 // PWM

// --- Sensor Ultrasónico HC-SR04 ---
#define TRIG_PIN A0 // Se usan pines analógicos como digitales si están disponibles
#define ECHO_PIN A1

// --- Parámetros ---
#define OBSTACLE_DISTANCE_CM 25     // Distancia de detección de obstáculo
#define OBSTACLE_AVOID_TIME 600     // ms para avanzar/retroceder/girar en evitado

// --- Variables ---
bool obstacle_avoidance_enabled = true; // Activado por defecto

unsigned long lastDistanceSent = 0;
const unsigned long DISTANCE_SEND_INTERVAL = 500; // ms

void setup() {
  // Motores
  pinMode(M1_IN1, OUTPUT); pinMode(M1_IN2, OUTPUT); pinMode(M1_ENA, OUTPUT);
  pinMode(M2_IN3, OUTPUT); pinMode(M2_IN4, OUTPUT); pinMode(M2_ENB, OUTPUT);

  // Sensor ultrasónico
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Serial.begin(9600); // ¡CUIDADO! Este Serial se usará para comunicar con el Nano 1.
                      // Si usas el Monitor Serial del IDE, desconecta el Nano 1 primero.
  stopMotors();
  Serial.println("Nano Secundario iniciado. Esperando comandos del Nano Principal.");
}

void loop() {
  // Checa obstáculo si la función está activada
  if (obstacle_avoidance_enabled && detectObstacle(OBSTACLE_DISTANCE_CM)) {
    avoidObstacle();
    return; // Si evade, no procesa comandos de movimiento adicionales por ahora
  }

  // Enviar distancia periódicamente al Nano 1
  unsigned long now = millis();
  if (now - lastDistanceSent >= DISTANCE_SEND_INTERVAL) {
    float dist = measureDistance();
    Serial.print("D:"); // Prefijo para que el Nano 1 pueda identificar el tipo de mensaje
    Serial.println(dist, 1); // Un decimal de precisión
    lastDistanceSent = now;
  }

  // Leer comando desde Nano 1
  if (Serial.available() >= 2) { // Espera al menos 2 bytes: comando + velocidad/estado
    char cmd = Serial.read();
    uint8_t value = Serial.read(); // Puede ser velocidad o un valor de 0/1 para enable/disable

    if (cmd == 'E') {
      obstacle_avoidance_enabled = value ? true : false;
      Serial.print("Evasion de obstaculos: ");
      Serial.println(obstacle_avoidance_enabled ? "ACTIVADA" : "DESACTIVADA");
      return; // Ya se procesó el comando
    }
    
    // Si no es 'E', debe ser un comando de movimiento con velocidad
    if (value < 0 || value > 255) {
      Serial.println("Velocidad no válida. Debe ser entre 0 y 255.");
      return;
    }

    processCommand(cmd, value); // 'value' ahora es 'speed'
    if (cmd == 'S') {
      Serial.println("Motores detenidos.");
    } else {
      Serial.print("Comando recibido: ");
      Serial.print(cmd);
      Serial.print(" con velocidad: ");
      Serial.println(value);
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

// --- Motor helper (para este Nano) ---
void setMotor(int in1, int in2, int ena, bool forward, uint8_t vel) {
  digitalWrite(in1, forward ? HIGH : LOW);
  digitalWrite(in2, forward ? LOW : HIGH);
  analogWrite(ena, vel); // Este Nano tiene los pines PWM de movilidad
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
  stopMotors();
  delay(1250);
  moveBackward(80);
  delay(OBSTACLE_AVOID_TIME);
  turnRight(85);
  delay(OBSTACLE_AVOID_TIME);
  moveForward(65);
  delay(OBSTACLE_AVOID_TIME);
  stopMotors();
  delay(200);
}