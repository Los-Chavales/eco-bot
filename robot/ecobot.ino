#include <WiFi.h>


// Configuración de la red WiFi
const char* ssid = "NOMBRE_RED";
const char* password = "CONTRASEÑA";

WiFiServer server(80);  // Servidor TCP en puerto 80

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("Conectado!");
  server.begin();
}


// Control de motores
#define IN1 14
#define IN2 12
#define IN3 27
#define IN4 26
#define ENA 32
#define ENB 33

void setupMotores() {
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT); pinMode(ENB, OUTPUT);
}

void avanzar() {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
  analogWrite(ENA, 200); analogWrite(ENB, 200);
}

void detener() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
}
void retroceder() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
  analogWrite(ENA, 200); analogWrite(ENB, 200);
}
void girarDerecha() {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
  analogWrite(ENA, 200); analogWrite(ENB, 200);
}
void girarIzquierda() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
  analogWrite(ENA, 200); analogWrite(ENB, 200);
}


//Control de cepillo
#define PIN_RELE_CEPILLO 25

void activarCepillo(bool estado) {
  digitalWrite(PIN_RELE_CEPILLO, estado ? HIGH : LOW);
}


// Servos (de compuerta y compactador)
#include <Servo.h>

Servo servoCompuerta;
Servo servoCompactador;

void setupServos() {
  servoCompuerta.attach(4);      // Pin ejemplo
  servoCompactador.attach(5);
}

void abrirCompuerta() {
  servoCompuerta.write(90);
}

void compactar() {
  servoCompactador.write(0);
  delay(1000);
  servoCompactador.write(90);
}


// Lectura de sensores ultrasónicos
#define TRIG1 18
#define ECHO1 19

long medirDistancia() {
  digitalWrite(TRIG1, LOW); delayMicroseconds(2);
  digitalWrite(TRIG1, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG1, LOW);
  long duracion = pulseIn(ECHO1, HIGH);
  return duracion * 0.034 / 2;
}


// Rección de comandos desde el servidor (PC)
void loop() {
  WiFiClient client = server.available();
  if (client) {
    String comando = client.readStringUntil('\n');
    comando.trim();
    if (comando == "AVANZAR") avanzar();
    else if (comando == "DETENER") detener();
    else if (comando == "CEPI_ON") activarCepillo(true);
    else if (comando == "CEPI_OFF") activarCepillo(false);
    else if (comando == "ABRIR") abrirCompuerta();
    else if (comando == "COMPACTAR") compactar();
  }
}
