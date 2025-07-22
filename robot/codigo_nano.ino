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

// --- Parámetros de Búsqueda de Desechos (Controlado por Nano) ---
// Duración del movimiento recto en el modo de búsqueda (1 minuto = 60000 ms)
const unsigned long SEARCH_STRAIGHT_DURATION = 60000; 
// Tiempo para girar en el modo de búsqueda (ajusta según sea necesario)
const unsigned long SEARCH_TURN_DURATION = 1500;     

// --- Variables ---
bool obstacle_avoidance_enabled = true; // Activado por defecto
bool searching_for_debris = false;      // Bandera para el modo de búsqueda
unsigned long search_start_time = 0;    // Tiempo de inicio del segmento de búsqueda

unsigned long lastDistanceSent = 0;
const unsigned long DISTANCE_SEND_INTERVAL = 500; // Intervalo en ms para enviar la distancia

void setup() {
  // Configuración de pines de los motores como salida
  pinMode(M1_IN1, OUTPUT); pinMode(M1_IN2, OUTPUT); pinMode(M1_ENA, OUTPUT);
  pinMode(M2_IN3, OUTPUT); pinMode(M2_IN4, OUTPUT); pinMode(M2_ENB, OUTPUT);

  // Configuración de pines del sensor ultrasónico
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Serial.begin(9600); // Inicia la comunicación serial con el ESP32
  stopMotors(); // Asegura que los motores estén detenidos al inicio
}

void loop() {
  // 1. Detección y evasión de obstáculos (tiene la máxima prioridad)
  if (obstacle_avoidance_enabled && detectObstacle(OBSTACLE_DISTANCE_CM)) {
    // Si se detecta un obstáculo durante la búsqueda, se detiene la búsqueda
    if (searching_for_debris) { 
        Serial.println("Obstaculo detectado durante busqueda!");
        searching_for_debris = false; // Detiene el modo de búsqueda
    }
    avoidObstacle(); // Ejecuta la rutina de evasión de obstáculos
    return; // Sale del loop para que la evasión no sea interrumpida
  }

  // 2. Lógica de búsqueda de desechos (solo si no hay evasión de obstáculos)
  if (searching_for_debris) {
    if (search_start_time == 0) { // Si es la primera vez que entra en modo de búsqueda
      search_start_time = millis(); // Guarda el tiempo de inicio
      moveForward(65); // Comienza a moverse hacia adelante
      Serial.println("Avanzando en busqueda...");
    } else if (millis() - search_start_time >= SEARCH_STRAIGHT_DURATION) {
      // Si ha pasado el tiempo de movimiento recto sin detectar obstáculos
      stopMotors(); // Detiene los motores
      delay(500); // Pequeña pausa
      turnRight(85); // Gira para cambiar de dirección
      delay(SEARCH_TURN_DURATION); // Duración del giro
      stopMotors(); // Detiene los motores después de girar
      delay(500); // Pequeña pausa
      search_start_time = millis(); // Reinicia el temporizador para el siguiente segmento recto
      moveForward(65); // Continúa avanzando en la nueva dirección
      Serial.println("Girando y continuando busqueda...");
    }
  }

  // 3. Envío periódico de la distancia al ESP32
  unsigned long now = millis();
  if (now - lastDistanceSent >= DISTANCE_SEND_INTERVAL) {
    float dist = measureDistance(); // Mide la distancia actual
    Serial.print("D:"); // Prefijo para identificar el mensaje como distancia
    Serial.println(dist, 1); // Envía la distancia con un decimal de precisión
    lastDistanceSent = now; // Actualiza el tiempo del último envío
  }

  // 4. Procesamiento de comandos recibidos desde el ESP32
  if (Serial.available() >= 2) { // Espera al menos 2 bytes (comando + velocidad/parámetro)
    char cmd = Serial.read(); // Lee el byte del comando
    uint8_t speed = Serial.read(); // Lee el byte de velocidad (o parámetro para 'E', 'X', 'Z')

    // Manejo del comando de activación/desactivación de evasión de obstáculos
    if (cmd == 'E') {
      obstacle_avoidance_enabled = speed ? true : false;
      Serial.print("Evasion de obstaculos: ");
      Serial.println(obstacle_avoidance_enabled ? "ACTIVADA" : "DESACTIVADA");
      // Si se desactiva la evasión, también se detiene la búsqueda para evitar conflictos
      if (!obstacle_avoidance_enabled && searching_for_debris) {
        searching_for_debris = false;
        stopMotors();
        Serial.println("Busqueda de desechos detenida por desactivacion de evasion.");
      }
      return; // Sale del loop después de procesar el comando 'E'
    }
    // Manejo del comando para iniciar la búsqueda de desechos
    else if (cmd == 'X') { 
      searching_for_debris = true;
      search_start_time = 0; // Se fuerza el inicio del movimiento en el siguiente loop
      Serial.println("Iniciando busqueda de desechos.");
      return; // Sale del loop después de procesar el comando 'X'
    }
    // Manejo del comando para detener la búsqueda de desechos
    else if (cmd == 'Z') { 
      searching_for_debris = false;
      stopMotors(); // Detiene los motores
      Serial.println("Busqueda de desechos detenida.");
      return; // Sale del loop después de procesar el comando 'Z'
    }
    
    // Si se recibe un comando de movimiento directo (F, B, L, R, S), se detiene la búsqueda autónoma
    if (searching_for_debris && strchr("FBLRS", cmd)) {
        searching_for_debris = false;
        Serial.println("Busqueda de desechos interrumpida por comando directo.");
    }

    // Validación de la velocidad para comandos de movimiento
    if (speed < 0 || speed > 255) {
      Serial.println("Velocidad no válida. Debe ser entre 0 y 255.");
      return; // Sale del loop si la velocidad es inválida
    }

    // Procesa el comando de movimiento (F, B, L, R, S)
    processCommand(cmd, speed);
    if (cmd == 'S') {
      // El mensaje "Motores detenidos." ya se imprime dentro de stopMotors()
    } else {
      Serial.print("Comando recibido: ");
      Serial.print(cmd);
      Serial.print(" con velocidad: ");
      Serial.println(speed);
    }
  }
}

// --- Rutinas de control de motores ---

// Función para procesar comandos de movimiento
void processCommand(char cmd, uint8_t vel) {
  switch (cmd) {
    case 'F': moveForward(vel); break; // Mover hacia adelante
    case 'B': moveBackward(vel); break; // Mover hacia atrás
    case 'L': turnLeft(vel); break;     // Girar a la izquierda
    case 'R': turnRight(vel); break;    // Girar a la derecha
    case 'S': stopMotors(); break;      // Detener motores
    default:  stopMotors();             // Por defecto, detener motores
  }
}

// Mover hacia adelante
void moveForward(uint8_t vel) {
  setMotor(M1_IN1, M1_IN2, M1_ENA, true, vel);  // Motor 1 hacia adelante
  setMotor(M2_IN3, M2_IN4, M2_ENB, true, vel);  // Motor 2 hacia adelante
}

// Mover hacia atrás
void moveBackward(uint8_t vel) {
  setMotor(M1_IN1, M1_IN2, M1_ENA, false, vel); // Motor 1 hacia atrás
  setMotor(M2_IN3, M2_IN4, M2_ENB, false, vel); // Motor 2 hacia atrás
}

// Girar a la izquierda (Motor 1 hacia atrás, Motor 2 hacia adelante)
void turnLeft(uint8_t vel) {
  setMotor(M1_IN1, M1_IN2, M1_ENA, false, vel);
  setMotor(M2_IN3, M2_IN4, M2_ENB, true, vel);
}

// Girar a la derecha (Motor 1 hacia adelante, Motor 2 hacia atrás)
void turnRight(uint8_t vel) {
  setMotor(M1_IN1, M1_IN2, M1_ENA, true, vel);
  setMotor(M2_IN3, M2_IN4, M2_ENB, false, vel);
}

// Detener ambos motores
void stopMotors() {
  setMotor(M1_IN1, M1_IN2, M1_ENA, true, 0); // Establece velocidad a 0 para Motor 1
  setMotor(M2_IN3, M2_IN4, M2_ENB, true, 0); // Establece velocidad a 0 para Motor 2
  Serial.println("Motores detenidos."); // Informa al ESP32 que los motores están detenidos
}

// Función auxiliar para controlar un motor individual
void setMotor(int in1, int in2, int ena, bool forward, uint8_t vel) {
  digitalWrite(in1, forward ? HIGH : LOW); // Establece la dirección del motor
  digitalWrite(in2, forward ? LOW : HIGH);
  analogWrite(ena, vel); // Establece la velocidad PWM del motor
}

// --- Funciones del sensor ultrasónico ---

// Detecta si hay un obstáculo dentro del límite de distancia
bool detectObstacle(float limit_cm) {
  float distance_cm = measureDistance(); // Mide la distancia
  return (distance_cm > 0 && distance_cm <= limit_cm); // Retorna true si hay un obstáculo
}

// Mide la distancia usando el sensor HC-SR04
float measureDistance() {
  long duration;
  float distance_cm;

  // Genera un pulso en el pin TRIG
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Mide la duración del pulso de retorno en el pin ECHO
  duration = pulseIn(ECHO_PIN, HIGH, 25000); // Timeout de 25ms (~4.3m)
  // Calcula la distancia en cm (velocidad del sonido ~0.034 cm/us)
  distance_cm = duration * 0.034 / 2;

  return distance_cm;
}

// --- Algoritmo de esquivar obstáculo ---
void avoidObstacle() {
  // 1. Detenerse
  stopMotors(); // Llama a stopMotors() que ya imprime "Motores detenidos."
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

  // 5. Detenerse y esperar
  stopMotors(); // Llama a stopMotors() que ya imprime "Motores detenidos."
  delay(200);
}