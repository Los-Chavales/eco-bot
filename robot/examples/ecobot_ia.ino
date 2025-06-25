#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// Configuración WiFi
const char* ssid = "TU_WIFI_SSID";         // Cambia por tu red WiFi
const char* password = "TU_WIFI_PASSWORD"; // Cambia por tu contraseña

// Configuración del servidor web
WebServer server(80);

// Pines para control de motores (puente H L298N)
// Motor izquierdo
const int motorIzqA = 2;  // IN1
const int motorIzqB = 4;  // IN2
const int pwmIzq = 5;     // ENA (PWM)

// Motor derecho  
const int motorDerA = 18; // IN3
const int motorDerB = 19; // IN4
const int pwmDer = 21;    // ENB (PWM)

// Configuración PWM
const int pwmFreq = 5000;
const int pwmResolution = 8;
const int pwmChannelIzq = 0;
const int pwmChannelDer = 1;

// Variables de control
int velocidadBase = 150;      // Velocidad base (0-255)
int velocidadGiro = 120;      // Velocidad para giros
bool robotActivo = true;
unsigned long ultimoComando = 0;
const unsigned long timeoutComando = 2000; // 2 segundos sin comandos = stop

// Estados del robot
enum EstadoRobot {
  DETENIDO,
  AVANZANDO,
  RETROCEDIENDO,
  GIRANDO_IZQUIERDA,
  GIRANDO_DERECHA,
  BUSCANDO
};

EstadoRobot estadoActual = DETENIDO;
unsigned long tiempoBusqueda = 0;
bool direccionBusqueda = true; // true = derecha, false = izquierda

void setup() {
  Serial.begin(115200);
  
  // Configurar pines de motores
  pinMode(motorIzqA, OUTPUT);
  pinMode(motorIzqB, OUTPUT);
  pinMode(motorDerA, OUTPUT);
  pinMode(motorDerB, OUTPUT);
  
  // Configurar PWM
  ledcSetup(pwmChannelIzq, pwmFreq, pwmResolution);
  ledcSetup(pwmChannelDer, pwmFreq, pwmResolution);
  ledcAttachPin(pwmIzq, pwmChannelIzq);
  ledcAttachPin(pwmDer, pwmChannelDer);
  
  // Detener motores inicialmente
  detenerMotores();
  
  // Conectar a WiFi
  WiFi.begin(ssid, password);
  Serial.println("Conectando a WiFi...");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.println("WiFi conectado!");
  Serial.print("IP del ESP32: ");
  Serial.println(WiFi.localIP());
  
  // Configurar rutas del servidor web
  server.on("/move_forward", HTTP_GET, manejarAvanzar);
  server.on("/move_backward", HTTP_GET, manejarRetroceder);
  server.on("/turn_left", HTTP_GET, manejarGirarIzquierda);
  server.on("/turn_right", HTTP_GET, manejarGirarDerecha);
  server.on("/stop", HTTP_GET, manejarDetener);
  server.on("/search", HTTP_GET, manejarBuscar);
  server.on("/status", HTTP_GET, manejarEstatus);
  server.on("/config", HTTP_POST, manejarConfiguracion);
  
  // Ruta por defecto
  server.onNotFound(manejarNoEncontrado);
  
  // Iniciar servidor
  server.begin();
  Serial.println("Servidor web iniciado");
  Serial.println("Comandos disponibles:");
  Serial.println("- /move_forward");
  Serial.println("- /move_backward");
  Serial.println("- /turn_left");
  Serial.println("- /turn_right");
  Serial.println("- /stop");
  Serial.println("- /search");
  Serial.println("- /status");
}

void loop() {
  // Manejar clientes web
  server.handleClient();
  
  // Verificar timeout de comandos
  if (millis() - ultimoComando > timeoutComando && estadoActual != DETENIDO) {
    Serial.println("Timeout - Deteniendo robot");
    cambiarEstado(DETENIDO);
  }
  
  // Manejar búsqueda automática
  if (estadoActual == BUSCANDO) {
    manejarBusquedaAutomatica();
  }
  
  delay(50); // Pequeña pausa
}

void cambiarEstado(EstadoRobot nuevoEstado) {
  if (estadoActual != nuevoEstado) {
    Serial.print("Cambiando estado de ");
    Serial.print(obtenerNombreEstado(estadoActual));
    Serial.print(" a ");
    Serial.println(obtenerNombreEstado(nuevoEstado));
    
    estadoActual = nuevoEstado;
    ejecutarAccionEstado();
  }
}

String obtenerNombreEstado(EstadoRobot estado) {
  switch (estado) {
    case DETENIDO: return "DETENIDO";
    case AVANZANDO: return "AVANZANDO";
    case RETROCEDIENDO: return "RETROCEDIENDO";
    case GIRANDO_IZQUIERDA: return "GIRANDO_IZQUIERDA";
    case GIRANDO_DERECHA: return "GIRANDO_DERECHA";
    case BUSCANDO: return "BUSCANDO";
    default: return "DESCONOCIDO";
  }
}

void ejecutarAccionEstado() {
  switch (estadoActual) {
    case DETENIDO:
      detenerMotores();
      break;
    case AVANZANDO:
      moverAdelante(velocidadBase);
      break;
    case RETROCEDIENDO:
      moverAtras(velocidadBase);
      break;
    case GIRANDO_IZQUIERDA:
      girarIzquierda(velocidadGiro);
      break;
    case GIRANDO_DERECHA:
      girarDerecha(velocidadGiro);
      break;
    case BUSCANDO:
      tiempoBusqueda = millis();
      if (direccionBusqueda) {
        girarDerecha(velocidadGiro / 2);
      } else {
        girarIzquierda(velocidadGiro / 2);
      }
      break;
  }
}

void manejarBusquedaAutomatica() {
  // Cambiar dirección cada 2 segundos
  if (millis() - tiempoBusqueda > 2000) {
    direccionBusqueda = !direccionBusqueda;
    tiempoBusqueda = millis();
    
    if (direccionBusqueda) {
      girarDerecha(velocidadGiro / 2);
    } else {
      girarIzquierda(velocidadGiro / 2);
    }
  }
}

// Funciones de control de motores
void moverAdelante(int velocidad) {
  // Motor izquierdo adelante
  digitalWrite(motorIzqA, HIGH);
  digitalWrite(motorIzqB, LOW);
  ledcWrite(pwmChannelIzq, velocidad);
  
  // Motor derecho adelante
  digitalWrite(motorDerA, HIGH);
  digitalWrite(motorDerB, LOW);
  ledcWrite(pwmChannelDer, velocidad);
}

void moverAtras(int velocidad) {
  // Motor izquierdo atrás
  digitalWrite(motorIzqA, LOW);
  digitalWrite(motorIzqB, HIGH);
  ledcWrite(pwmChannelIzq, velocidad);
  
  // Motor derecho atrás
  digitalWrite(motorDerA, LOW);
  digitalWrite(motorDerB, HIGH);
  ledcWrite(pwmChannelDer, velocidad);
}

void girarIzquierda(int velocidad) {
  // Motor izquierdo atrás
  digitalWrite(motorIzqA, LOW);
  digitalWrite(motorIzqB, HIGH);
  ledcWrite(pwmChannelIzq, velocidad);
  
  // Motor derecho adelante
  digitalWrite(motorDerA, HIGH);
  digitalWrite(motorDerB, LOW);
  ledcWrite(pwmChannelDer, velocidad);
}

void girarDerecha(int velocidad) {
  // Motor izquierdo adelante
  digitalWrite(motorIzqA, HIGH);
  digitalWrite(motorIzqB, LOW);
  ledcWrite(pwmChannelIzq, velocidad);
  
  // Motor derecho atrás
  digitalWrite(motorDerA, LOW);
  digitalWrite(motorDerB, HIGH);
  ledcWrite(pwmChannelDer, velocidad);
}

void detenerMotores() {
  digitalWrite(motorIzqA, LOW);
  digitalWrite(motorIzqB, LOW);
  digitalWrite(motorDerA, LOW);
  digitalWrite(motorDerB, LOW);
  ledcWrite(pwmChannelIzq, 0);
  ledcWrite(pwmChannelDer, 0);
}

// Manejadores de rutas HTTP
void manejarAvanzar() {
  ultimoComando = millis();
  cambiarEstado(AVANZANDO);
  server.send(200, "text/plain", "Avanzando");
}

void manejarRetroceder() {
  ultimoComando = millis();
  cambiarEstado(RETROCEDIENDO);
  server.send(200, "text/plain", "Retrocediendo");
}

void manejarGirarIzquierda() {
  ultimoComando = millis();
  cambiarEstado(GIRANDO_IZQUIERDA);
  server.send(200, "text/plain", "Girando izquierda");
}

void manejarGirarDerecha() {
  ultimoComando = millis();
  cambiarEstado(GIRANDO_DERECHA);
  server.send(200, "text/plain", "Girando derecha");
}

void manejarDetener() {
  ultimoComando = millis();
  cambiarEstado(DETENIDO);
  server.send(200, "text/plain", "Detenido");
}

void manejarBuscar() {
  ultimoComando = millis();
  cambiarEstado(BUSCANDO);
  server.send(200, "text/plain", "Buscando");
}

void manejarEstatus() {
  // Crear JSON con el estado actual
  StaticJsonDocument<200> doc;
  doc["estado"] = obtenerNombreEstado(estadoActual);
  doc["velocidad_base"] = velocidadBase;
  doc["velocidad_giro"] = velocidadGiro;
  doc["wifi_signal"] = WiFi.RSSI();
  doc["uptime"] = millis();
  doc["ip"] = WiFi.localIP().toString();
  
  String response;
  serializeJson(doc, response);
  
  server.send(200, "application/json", response);
}

void manejarConfiguracion() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, body);
    
    if (!error) {
      if (doc.containsKey("velocidad_base")) {
        velocidadBase = doc["velocidad_base"];
        velocidadBase = constrain(velocidadBase, 50, 255);
      }
      
      if (doc.containsKey("velocidad_giro")) {
        velocidadGiro = doc["velocidad_giro"];
        velocidadGiro = constrain(velocidadGiro, 50, 255);
      }
      
      server.send(200, "text/plain", "Configuración actualizada");
      Serial.println("Configuración actualizada:");
      Serial.print("Velocidad base: ");
      Serial.println(velocidadBase);
      Serial.print("Velocidad giro: ");
      Serial.println(velocidadGiro);
    } else {
      server.send(400, "text/plain", "JSON inválido");
    }
  } else {
    server.send(400, "text/plain", "Sin datos");
  }
}

void manejarNoEncontrado() {
  String mensaje = "Ruta no encontrada\n\n";
  mensaje += "Comandos disponibles:\n";
  mensaje += "GET /move_forward - Avanzar\n";
  mensaje += "GET /move_backward - Retroceder\n";
  mensaje += "GET /turn_left - Girar izquierda\n";
  mensaje += "GET /turn_right - Girar derecha\n";
  mensaje += "GET /stop - Detener\n";
  mensaje += "GET /search - Buscar\n";
  mensaje += "GET /status - Estado actual\n";
  mensaje += "POST /config - Configurar velocidades\n";
  
  server.send(404, "text/plain", mensaje);
}