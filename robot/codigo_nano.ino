// Pines para L298N
#define M3_IN1 7
#define M3_IN2 8
#define M3_ENA 9
#define M4_IN3 11
#define M4_IN4 12
#define M4_ENB 10

// Velocidades PWM
#define SPEED_SLOW 65   // 50% de 255
#define SPEED_FAST 75   // 100%

char lastCommand = 'S';

void setup() {
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
  setMotor(M3_IN1, M3_IN2, M3_ENA, true, vel);
  setMotor(M4_IN3, M4_IN4, M4_ENB, true, vel);
}

void moveBackward(int vel) {
  setMotor(M3_IN1, M3_IN2, M3_ENA, false, vel);
  setMotor(M4_IN3, M4_IN4, M4_ENB, false, vel);
}

void turnLeft(int vel) {
  // Motores lado derecho adelante, izquierdo atrás
  setMotor(M3_IN1, M3_IN2, M3_ENA, false, vel);
  setMotor(M4_IN3, M4_IN4, M4_ENB, true, vel);
}

void turnRight(int vel) {
  setMotor(M3_IN1, M3_IN2, M3_ENA, true, vel);
  setMotor(M4_IN3, M4_IN4, M4_ENB, false, vel);
}

void stopMotors() {
  setMotor(M3_IN1, M3_IN2, M3_ENA, true, 0);
  setMotor(M4_IN3, M4_IN4, M4_ENB, true, 0);
}

void setMotor(int in1, int in2, int ena, bool forward, int vel) {
  digitalWrite(in1, forward ? HIGH : LOW);
  digitalWrite(in2, forward ? LOW : HIGH);
  analogWrite(ena, vel);
}