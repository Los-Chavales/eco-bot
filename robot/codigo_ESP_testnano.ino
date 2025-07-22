#include <WiFi.h>

// Cambia estos datos por los de tu red WiFi
const char* ssid = "snowden";
const char* password = "qwertyasdfghzxcvb54321";

// Telnet server
WiFiServer telnetServer(23);
WiFiClient telnetClient;

// Serial2: TX2=GPIO17 → RX del Nano, RX2=GPIO16 ← TX del Nano
#define RX2 16
#define TX2 17

// LED integrado
#define LED_BUILTIN 2 

unsigned long lastTelnetInputTime = 0;
String telnetMessageQueue = "";

// Variables para la lógica de control del robot
float current_distance = 0.0; // Última distancia recibida del Nano
bool nano_is_searching = false; // Estado de la búsqueda en el Nano (true si el Nano está en modo 'X')

void setup() {
  Serial.begin(115200); // Inicia la comunicación serial USB
  Serial2.begin(9600, SERIAL_8N1, RX2, TX2); // Inicia la comunicación serial con el Nano

  pinMode(LED_BUILTIN, OUTPUT); // Configura el LED integrado como salida
  digitalWrite(LED_BUILTIN, HIGH); // Enciende el LED (indicador de conexión WiFi)

  // Conectar a WiFi
  WiFi.mode(WIFI_STA); // Modo estación (cliente)
  WiFi.begin(ssid, password); // Intenta conectar a la red WiFi

  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) { // Espera hasta que se conecte a WiFi
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConectado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP()); // Imprime la dirección IP asignada
  digitalWrite(LED_BUILTIN, LOW); // Apaga el LED una vez conectado

  // Iniciar Telnet
  telnetServer.begin(); // Inicia el servidor Telnet en el puerto 23
  telnetServer.setNoDelay(true); // Deshabilita el algoritmo Nagle para menor latencia
  Serial.println("Servidor Telnet iniciado. Puerto 23");
}

String nanoLine = ""; // Buffer para almacenar la línea de datos del Nano
unsigned long lastNanoData = millis(); // Tiempo del último dato recibido del Nano
unsigned long lastRssiReport = 0; // Para controlar el envío periódico de RSSI

void loop() {
  // Reconexión Wi-Fi automática
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado. Intentando reconectar...");
    digitalWrite(LED_BUILTIN, HIGH); // Enciende el LED para indicar desconexión
    WiFi.disconnect(); // Desconecta y vuelve a intentar
    WiFi.reconnect();
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      delay(1000);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nReconectado a WiFi!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      digitalWrite(LED_BUILTIN, LOW); // Apaga el LED al reconectar
    } else {
      Serial.println("\nNo se pudo reconectar a WiFi.");
    }
  }

  // Reportar intensidad de señal Wi-Fi cada 5 segundos
  if (millis() - lastRssiReport > 5000) {
    lastRssiReport = millis();
    int rssi = WiFi.RSSI(); // Obtiene la intensidad de la señal
    Serial.print("[WiFi] Intensidad de señal (RSSI): ");
    Serial.print(rssi);
    Serial.println(" dBm");
  }

  // Aceptar nuevas conexiones Telnet
  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      if (telnetClient) telnetClient.stop(); // Detiene cualquier cliente anterior
      telnetClient = telnetServer.available(); // Acepta la nueva conexión
      telnetClient.println("Bienvenido al robot por Telnet!");
      telnetClient.println("Comandos: F,B,L,R,S + [velocidad opcional 0-255] (ej: F200)");
      telnetClient.println("E1=activar evasión, E0=desactivar evasión");
      telnetClient.println("X=Iniciar busqueda de desechos, Z=Detener busqueda de desechos");
    } else {
      // Rechazar conexiones adicionales si ya hay un cliente conectado
      WiFiClient newClient = telnetServer.available();
      newClient.println("Ya hay un cliente conectado.");
      newClient.stop();
    }
  }

  // Procesar comandos recibidos por Telnet
  if (telnetClient && telnetClient.connected() && telnetClient.available()) {
    lastTelnetInputTime = millis(); // Actualiza el tiempo de la última entrada Telnet
    String input = telnetClient.readStringUntil('\n'); // Lee la línea completa
    input.trim(); // Elimina espacios en blanco al inicio y final
    if (input.length() > 0) {
      char cmd = toupper(input[0]); // Convierte el primer carácter a mayúscula
      if (cmd == 'E') {
        // Comando para activar/desactivar la evasión de obstáculos en el Nano
        uint8_t enable = (input.length() > 1 && input[1] == '1') ? 1 : 0;
        Serial2.write('E'); // Envía el comando 'E'
        Serial2.write(enable); // Envía 1 o 0 como el byte de parámetro
        telnetClient.printf("Evasion de obstaculos: %s\n", enable ? "ACTIVADA" : "DESACTIVADA");
        Serial.printf("Comando Telnet: E%d\n", enable);
        // Si se desactiva la evasión, también se detiene la búsqueda autónoma en el ESP32
        if (!enable) {
            nano_is_searching = false;
        }
      } else if (cmd == 'X') { // Comando para iniciar la búsqueda de desechos en el Nano
        Serial2.write('X'); // Envía el comando 'X'
        Serial2.write(0); // Envía un byte dummy (no se usa en el Nano para 'X')
        nano_is_searching = true; // Actualiza el estado de búsqueda en el ESP32
        telnetClient.println("Comando: Iniciar busqueda de desechos.");
        Serial.println("Comando Telnet: Iniciar busqueda de desechos.");
      } else if (cmd == 'Z') { // Comando para detener la búsqueda de desechos en el Nano
        Serial2.write('Z'); // Envía el comando 'Z'
        Serial2.write(0); // Envía un byte dummy
        nano_is_searching = false; // Actualiza el estado de búsqueda en el ESP32
        telnetClient.println("Comando: Detener busqueda de desechos.");
        Serial.println("Comando Telnet: Detener busqueda de desechos.");
      } else if (strchr("FBLRS", cmd)) { // Comandos de movimiento directo (Forward, Backward, Left, Right, Stop)
        if (input.length() <= 1) {
          telnetClient.println("Comando inválido: velocidad requerida");
        } else {
          int v = input.substring(1).toInt(); // Extrae la velocidad del comando
          if (v < 0 || v > 255) {
            telnetClient.println("Comando inválido: velocidad fuera de rango (0-255)");
          } else {
            uint8_t speed = v;
            Serial2.write(cmd); // Envía el comando de movimiento
            Serial2.write(speed); // Envía la velocidad
            // Si se envía un comando directo de movimiento, se detiene la búsqueda autónoma en el ESP32
            nano_is_searching = false; 
            telnetClient.printf("Enviado: %c %d\n", cmd, speed);
            Serial.printf("Comando Telnet: %c %d\n", cmd, speed);
          }
        }
      } else {
        telnetClient.print("Comando inválido: ");
        telnetClient.println(input);
      }
    }
  }

  // Leer comandos desde Serial USB (PC)
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      char cmd = toupper(input[0]);
      if (cmd == 'E') {
        uint8_t enable = (input.length() > 1 && input[1] == '1') ? 1 : 0;
        Serial2.write('E');
        Serial2.write(enable);
        Serial.printf("Enviado desde Serial: E%d\n", enable);
        if (!enable) {
            nano_is_searching = false;
        }
      } else if (cmd == 'X') { // Iniciar búsqueda de desechos
        Serial2.write('X');
        Serial2.write(0);
        nano_is_searching = true;
        Serial.println("Comando desde Serial: Iniciar busqueda de desechos.");
      } else if (cmd == 'Z') { // Detener búsqueda de desechos
        Serial2.write('Z');
        Serial2.write(0);
        nano_is_searching = false;
        Serial.println("Comando desde Serial: Detener busqueda de desechos.");
      } else if (strchr("FBLRS", cmd)) {
        if (input.length() <= 1) {
          Serial.println("Comando inválido desde Serial: velocidad requerida");
        } else {
          int v = input.substring(1).toInt();
          if (v < 0 || v > 255) {
            Serial.println("Comando inválido desde Serial: velocidad fuera de rango (0-255)");
          } else {
            uint8_t speed = v;
            Serial2.write(cmd);
            Serial2.write(speed);
            nano_is_searching = false; // Detener búsqueda autónoma
            Serial.printf("Enviado desde Serial: %c %d\n", cmd, speed);
          }
        }
      } else {
        Serial.print("Comando inválido desde Serial: ");
        Serial.println(input);
      }
    }
  }

  // Leer datos del Nano y acumular mensajes en cola para Telnet
  while (Serial2.available()) {
    char c = Serial2.read();
    lastNanoData = millis(); // Actualiza el tiempo del último dato del Nano
    if (c == '\n' || c == '\r') { // Si se recibe un salto de línea o retorno de carro
      if (nanoLine.length() > 0) {
        String mensaje;
        if (nanoLine.startsWith("D:")) {
          // Si el mensaje es una distancia, parsearla
          current_distance = nanoLine.substring(2).toFloat();
          mensaje = "[Nano] Distancia: " + String(current_distance, 1) + " cm\n";
        } else if (nanoLine.equals("Motores detenidos.")) {
            mensaje = "[Nano] " + nanoLine + "\n";
            // Si el Nano reporta que los motores están detenidos y el ESP32 pensaba que estaba buscando,
            // significa que el Nano encontró un obstáculo o fue detenido por otro comando.
            if (nano_is_searching) {
                telnetClient.println("Nano: Busqueda detenida, posible obstaculo detectado o comando de parada.");
                Serial.println("Nano: Busqueda detenida, posible obstaculo detectado o comando de parada.");
                nano_is_searching = false; // El ESP32 también detiene su estado de búsqueda
            }
        } else if (nanoLine.equals("Obstaculo detectado durante busqueda!")) {
            mensaje = "[Nano] " + nanoLine + "\n";
            nano_is_searching = false; // Nano ha detenido la búsqueda por obstáculo
        }
        else {
          mensaje = "[Nano] " + nanoLine + "\n";
        }
        Serial.print(mensaje); // Siempre envía el mensaje al Serial USB
        telnetMessageQueue += mensaje; // Acumula el mensaje para enviarlo por Telnet
        nanoLine = ""; // Limpia el buffer de la línea del Nano
      }
    } else {
      nanoLine += c; // Añade el carácter al buffer
    }
  }

  // Enviar mensajes en cola a Telnet si han pasado 2 segundos sin entrada por Telnet
  if (telnetClient && telnetClient.connected() && telnetMessageQueue.length() > 0 &&
      (millis() - lastTelnetInputTime > 2000)) {
    telnetClient.print(telnetMessageQueue); // Envía toda la cola acumulada
    telnetClient.flush(); // Asegura que los datos se envíen de inmediato
    telnetMessageQueue = ""; // Limpia la cola después de enviar
  }
}
