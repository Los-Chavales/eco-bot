#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

const char* ssid = "DARTHNEO_WIFI";
const char* password = "DARTHNEO_PASSWORD";

WebServer server(80);

void setup() {
  Serial.begin(115200);
  
  // Configurar pines de motores
  // [Agregar configuración específica de tus motores]
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Conectando a WiFi...");
  }
  Serial.println("WiFi conectado");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  
  server.on("/command", HTTP_POST, handleCommand);
  server.begin();
}

void loop() {
  server.handleClient();
}

void handleCommand() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, server.arg("plain"));
    
    String command = doc["command"];
    Serial.println("Comando recibido: " + command);
    
    // Ejecutar comando
    executeMovement(command);
    
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  }
}

void executeMovement(String command) {
  if (command == "FORWARD") {
    // Mover hacia adelante
  } else if (command == "LEFT") {
    // Girar a la izquierda
  } else if (command == "RIGHT") {
    // Girar a la derecha
  } else if (command == "STOP") {
    // Detener motores
  } else if (command == "COLLECT") {
    // Activar mecanismo de recolección
  }
}