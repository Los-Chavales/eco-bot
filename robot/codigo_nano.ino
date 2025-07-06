// Pines para L298N
#define M1_IN1 2
#define M1_IN2 3
#define M1_ENA 5
#define M2_IN3 4
#define M2_IN4 7
#define M2_ENB 6

#define M3_IN1 8
#define M3_IN2 9
#define M3_ENA 10
#define M4_IN3 11
#define M4_IN4 12
#define M4_ENB 13

// Velocidades PWM
#define SPEED_SLOW 128   // 50% de 255
#define SPEED_FAST 255   // 100%

char lastCommand = 'S';

void setup() {
  // Puente H 1
  pinMode(M1_IN1, OUTPUT); pinMode(M1_IN2, OUTPUT); pinMode(M1_ENA, OUTPUT);
  pinMode(M2_IN3, OUTPUT); pinMode(M2_IN4, OUTPUT); pinMode(M2_ENB, OUTPUT);
  // Puente H 2
  pinMode(M3_IN1, OUTPUT); pinMode(M3_IN2, OUTPUT); pinMode(M3_ENA, OUTPUT);
  pinMode(M4_IN3, OUTPUT); pinMode(M4_IN4, OUTPUT); pinMode(M4_ENB, OUTPUT);

  Serial.begin(9600); // Comunicación con ESP32
  stopMotors();
}

void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd != lastCommand) {
      lastCommand = cmd;
      processCommand(cmd);
    }
  }
}

void processCommand(char cmd) {
  bool fast = false;
  // minúsculas = rápido
  if (cmd >= 'a' && cmd <= 'z') {
    fast = true;
    cmd = cmd - 32; // Convierte a mayúscula
  }
  int vel = fast ? SPEED_FAST : SPEED_SLOW;

  switch (cmd) {
    case 'F': moveForward(vel); break;
    case 'B': moveBackward(vel); break;
    case 'L': turnLeft(vel); break;
    case 'R': turnRight(vel); break;
    default:  stopMotors();
  }
}

// Funciones control de motores

void moveForward(int vel) {
  // Ambos lados adelante
  setMotor(M1_IN1, M1_IN2, M1_ENA, true, vel);
  setMotor(M2_IN3, M2_IN4, M2_ENB, true, vel);
  setMotor(M3_IN1, M3_IN2, M3_ENA, true, vel);
  setMotor(M4_IN3, M4_IN4, M4_ENB, true, vel);
}

void moveBackward(int vel) {
  setMotor(M1_IN1, M1_IN2, M1_ENA, false, vel);
  setMotor(M2_IN3, M2_IN4, M2_ENB, false, vel);
  setMotor(M3_IN1, M3_IN2, M3_ENA, false, vel);
  setMotor(M4_IN3, M4_IN4, M4_ENB, false, vel);
}

void turnLeft(int vel) {
  // Motores lado derecho adelante, izquierdo atrás
  setMotor(M1_IN1, M1_IN2, M1_ENA, false, vel);
  setMotor(M2_IN3, M2_IN4, M2_ENB, false, vel);
  setMotor(M3_IN1, M3_IN2, M3_ENA, true, vel);
  setMotor(M4_IN3, M4_IN4, M4_ENB, true, vel);
}

void turnRight(int vel) {
  setMotor(M1_IN1, M1_IN2, M1_ENA, true, vel);
  setMotor(M2_IN3, M2_IN4, M2_ENB, true, vel);
  setMotor(M3_IN1, M3_IN2, M3_ENA, false, vel);
  setMotor(M4_IN3, M4_IN4, M4_ENB, false, vel);
}

void stopMotors() {
  setMotor(M1_IN1, M1_IN2, M1_ENA, true, 0);
  setMotor(M2_IN3, M2_IN4, M2_ENB, true, 0);
  setMotor(M3_IN1, M3_IN2, M3_ENA, true, 0);
  setMotor(M4_IN3, M4_IN4, M4_ENB, true, 0);
}

void setMotor(int in1, int in2, int ena, bool forward, int vel) {
  digitalWrite(in1, forward ? HIGH : LOW);
  digitalWrite(in2, forward ? LOW : HIGH);
  analogWrite(ena, vel);
}