#!/usr/bin/env python3
"""
Interfaz de control para robot recolector de basura
Compatible con el código ESP32 existente
"""

import requests
import json
import time
import logging
from flask import Flask, render_template_string, request, jsonify
import threading

# Configuración básica de logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

class RobotControlInterface:
    def __init__(self, esp32_ip="192.168.0.115", esp32_port=80):
        self.esp32_ip = esp32_ip
        self.esp32_port = esp32_port
        self.esp32_url = f"http://{esp32_ip}:{esp32_port}/command"
        self.last_command = None
        self.evasion_enabled = False
        self.gate_open = False
        
        # Flask app
        self.app = Flask(__name__)
        self.setup_routes()
        
    def setup_routes(self):
        @self.app.route('/')
        def index():
            return render_template_string(HTML_TEMPLATE)
        
        @self.app.route('/send_command', methods=['POST'])
        def send_command():
            try:
                data = request.get_json()
                command = data.get('command', '').upper()
                
                # Validar comando
                valid_commands = {
                    "FORWARD", "LEFT", "RIGHT", "STOP", "COLLECT", 
                    "OPENB", "CLOSEB", "EVASION_ON", "EVASION_OFF", "PUSH", "BACKWARD"
                }
                
                if command not in valid_commands:
                    return jsonify({"status": "error", "message": f"Comando inválido: {command}"})
                
                # Enviar comando al ESP32
                success, message = self.send_command_to_esp32(command)
                
                # Actualizar estado local
                if success:
                    self.last_command = command
                    if command == "EVASION_ON":
                        self.evasion_enabled = True
                    elif command == "EVASION_OFF":
                        self.evasion_enabled = False
                    elif command == "OPENB":
                        self.gate_open = True
                    elif command == "CLOSEB":
                        self.gate_open = False
                
                return jsonify({
                    "status": "success" if success else "error",
                    "message": message,
                    "last_command": self.last_command,
                    "evasion_enabled": self.evasion_enabled,
                    "gate_open": self.gate_open
                })
                
            except Exception as e:
                logging.error(f"Error procesando comando: {e}")
                return jsonify({"status": "error", "message": str(e)})
        
        @self.app.route('/update_ip', methods=['POST'])
        def update_ip():
            try:
                data = request.get_json()
                new_ip = data.get('ip', '').strip()
                
                if not new_ip:
                    return jsonify({"status": "error", "message": "IP no puede estar vacía"})
                
                # Validar formato básico de IP
                parts = new_ip.split('.')
                if len(parts) != 4 or not all(part.isdigit() and 0 <= int(part) <= 255 for part in parts):
                    return jsonify({"status": "error", "message": "Formato de IP inválido"})
                
                self.esp32_ip = new_ip
                self.esp32_url = f"http://{new_ip}:{self.esp32_port}/command"
                
                logging.info(f"IP actualizada a: {new_ip}")
                return jsonify({
                    "status": "success", 
                    "message": f"IP actualizada a {new_ip}",
                    "new_ip": new_ip
                })
                
            except Exception as e:
                logging.error(f"Error actualizando IP: {e}")
                return jsonify({"status": "error", "message": str(e)})
        
        @self.app.route('/get_status')
        def get_status():
            return jsonify({
                "esp32_ip": self.esp32_ip,
                "esp32_port": self.esp32_port,
                "last_command": self.last_command,
                "evasion_enabled": self.evasion_enabled,
                "gate_open": self.gate_open
            })
    
    def send_command_to_esp32(self, command):
        """Envía comando al ESP32 usando el mismo formato que el servidor original"""
        try:
            payload = {
                "command": command,
                "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S")
            }
            
            logging.info(f"Enviando comando: {command} a {self.esp32_url}")
            response = requests.post(self.esp32_url, json=payload, timeout=5)
            
            if response.status_code == 200:
                logging.info(f"Comando {command} enviado correctamente")
                return True, f"Comando {command} ejecutado correctamente"
            else:
                error_msg = f"Error HTTP {response.status_code}"
                logging.error(error_msg)
                return False, error_msg
                
        except requests.exceptions.Timeout:
            error_msg = "Timeout - ESP32 no responde"
            logging.error(error_msg)
            return False, error_msg
        except requests.exceptions.ConnectionError:
            error_msg = f"No se puede conectar al ESP32 en {self.esp32_ip}"
            logging.error(error_msg)
            return False, error_msg
        except Exception as e:
            error_msg = f"Error enviando comando: {str(e)}"
            logging.error(error_msg)
            return False, error_msg
    
    def run(self, host='0.0.0.0', port=5000, debug=False):
        """Ejecutar la interfaz web"""
        logging.info(f"Iniciando interfaz de control en http://{host}:{port}")
        logging.info(f"ESP32 configurado en: {self.esp32_ip}:{self.esp32_port}")
        self.app.run(host=host, port=port, debug=debug)

# Template HTML para la interfaz
HTML_TEMPLATE = """
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Control Robot Recolector</title>
    <style>
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            margin: 0;
            padding: 20px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            color: white;
        }
        
        .container {
            max-width: 800px;
            margin: 0 auto;
            background: rgba(255, 255, 255, 0.1);
            backdrop-filter: blur(10px);
            border-radius: 20px;
            padding: 30px;
            box-shadow: 0 20px 40px rgba(0, 0, 0, 0.1);
        }
        
        h1 {
            text-align: center;
            margin-bottom: 30px;
            font-size: 2.5em;
            text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.3);
        }
        
        .control-section {
            background: rgba(255, 255, 255, 0.05);
            border-radius: 15px;
            padding: 20px;
            margin-bottom: 20px;
            border: 1px solid rgba(255, 255, 255, 0.1);
        }
        
        .section-title {
            font-size: 1.3em;
            font-weight: bold;
            margin-bottom: 15px;
            color: #fff;
            border-bottom: 2px solid rgba(255, 255, 255, 0.2);
            padding-bottom: 5px;
        }
        
        .movement-controls {
            display: grid;
            grid-template-columns: 1fr 1fr 1fr;
            grid-template-rows: 1fr 1fr 1fr;
            gap: 10px;
            max-width: 300px;
            margin: 0 auto 20px;
        }
        
        .btn {
            padding: 15px 20px;
            border: none;
            border-radius: 10px;
            font-size: 16px;
            font-weight: bold;
            cursor: pointer;
            transition: all 0.3s ease;
            background: linear-gradient(145deg, #7b68ee, #6a5acd);
            color: white;
            box-shadow: 0 5px 15px rgba(0, 0, 0, 0.2);
            min-height: 60px;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        
        .btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 8px 25px rgba(0, 0, 0, 0.3);
            background: linear-gradient(145deg, #8a79f7, #7b68ee);
        }
        
        .btn:active {
            transform: translateY(0);
            box-shadow: 0 3px 10px rgba(0, 0, 0, 0.2);
        }
        
        .btn-forward { grid-column: 2; grid-row: 1; }
        .btn-left { grid-column: 1; grid-row: 2; }
        .btn-stop { 
            grid-column: 2; 
            grid-row: 2; 
            background: linear-gradient(145deg, #ff6b6b, #ee5a52);
        }
        .btn-right { grid-column: 3; grid-row: 2; }
        .btn-backward { grid-column: 2; grid-row: 3; }
        
        .btn-stop:hover {
            background: linear-gradient(145deg, #ff8e8e, #ff6b6b);
        }
        
        .action-controls {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin-bottom: 20px;
        }
        
        .btn-collect {
            background: linear-gradient(145deg, #4ecdc4, #44a08d);
        }
        
        .btn-collect:hover {
            background: linear-gradient(145deg, #5fd9d0, #4ecdc4);
        }
        
        .btn-push {
            background: linear-gradient(145deg, #ffa726, #ff9800);
        }
        
        .btn-push:hover {
            background: linear-gradient(145deg, #ffb74d, #ffa726);
        }
        
        .gate-controls {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 15px;
        }
        
        .btn-gate-open {
            background: linear-gradient(145deg, #66bb6a, #4caf50);
        }
        
        .btn-gate-close {
            background: linear-gradient(145deg, #ef5350, #f44336);
        }
        
        .evasion-control {
            text-align: center;
        }
        
        .toggle-switch {
            position: relative;
            display: inline-block;
            width: 80px;
            height: 40px;
            margin: 0 10px;
        }
        
        .toggle-switch input {
            opacity: 0;
            width: 0;
            height: 0;
        }
        
        .slider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: #ccc;
            transition: .4s;
            border-radius: 34px;
        }
        
        .slider:before {
            position: absolute;
            content: "";
            height: 32px;
            width: 32px;
            left: 4px;
            bottom: 4px;
            background-color: white;
            transition: .4s;
            border-radius: 50%;
        }
        
        input:checked + .slider {
            background: linear-gradient(145deg, #4caf50, #45a049);
        }
        
        input:checked + .slider:before {
            transform: translateX(40px);
        }
        
        .config-section {
            display: flex;
            align-items: center;
            gap: 10px;
            flex-wrap: wrap;
        }
        
        .ip-input {
            padding: 10px;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            background: rgba(255, 255, 255, 0.9);
            color: #333;
            min-width: 150px;
        }
        
        .status-bar {
            background: rgba(0, 0, 0, 0.2);
            border-radius: 10px;
            padding: 15px;
            margin-top: 20px;
            font-family: 'Courier New', monospace;
        }
        
        .status-item {
            margin: 5px 0;
        }
        
        .status-success {
            color: #4caf50;
        }
        
        .status-error {
            color: #f44336;
        }
        
        .status-info {
            color: #2196f3;
        }
        
        @media (max-width: 600px) {
            .container {
                padding: 20px;
                margin: 10px;
            }
            
            .movement-controls {
                max-width: 250px;
            }
            
            .btn {
                padding: 12px 15px;
                font-size: 14px;
                min-height: 50px;
            }
            
            .action-controls {
                grid-template-columns: 1fr;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🤖 Robot Recolector Control</h1>
        
        <!-- Controles de Movimiento -->
        <div class="control-section">
            <div class="section-title">🎮 Controles de Movimiento</div>
            <div class="movement-controls">
                <button class="btn btn-forward" onclick="sendCommand('FORWARD')">▲<br>ADELANTE</button>
                <button class="btn btn-left" onclick="sendCommand('LEFT')">◄<br>IZQUIERDA</button>
                <button class="btn btn-stop" onclick="sendCommand('STOP')">⏹<br>STOP</button>
                <button class="btn btn-right" onclick="sendCommand('RIGHT')">►<br>DERECHA</button>
                <button class="btn btn-backward" onclick="sendCommand('BACKWARD')" style="grid-column:2;grid-row:3;">
                    ▼<br>ATRÁS
                </button>
            </div>
        </div>
        
        <!-- Controles de Acción -->
        <div class="control-section">
            <div class="section-title">⚡ Acciones</div>
            <div class="action-controls">
                <button class="btn btn-collect" onclick="sendCommand('COLLECT')">
                    🗑️ RECOLECTAR
                </button>
                <button class="btn btn-push" onclick="sendCommand('PUSH')">
                    📤 EXPULSAR BASURA
                </button>
            </div>
        </div>
        
        <!-- Control de Compuerta -->
        <div class="control-section">
            <div class="section-title">🚪 Compuerta Trasera</div>
            <div class="gate-controls">
                <button class="btn btn-gate-open" onclick="sendCommand('OPENB')">
                    🔓 ABRIR
                </button>
                <button class="btn btn-gate-close" onclick="sendCommand('CLOSEB')">
                    🔒 CERRAR
                </button>
            </div>
        </div>
        
        <!-- Control de Evasión -->
        <div class="control-section">
            <div class="section-title">🛡️ Evasión de Obstáculos</div>
            <div class="evasion-control">
                <span>Desactivado</span>
                <label class="toggle-switch">
                    <input type="checkbox" id="evasionToggle" onchange="toggleEvasion()">
                    <span class="slider"></span>
                </label>
                <span>Activado</span>
            </div>
        </div>
        
        <!-- Configuración -->
        <div class="control-section">
            <div class="section-title">⚙️ Configuración</div>
            <div class="config-section">
                <label>IP del ESP32:</label>
                <input type="text" id="ipInput" class="ip-input" value="192.168.0.115" placeholder="192.168.0.115">
                <button class="btn" onclick="updateIP()" style="min-height: 40px;">
                    📡 ACTUALIZAR IP
                </button>
            </div>
        </div>
        
        <!-- Barra de Estado -->
        <div class="status-bar">
            <div class="section-title">📊 Estado del Sistema</div>
            <div id="status" class="status-item status-info">
                Listo para recibir comandos...
            </div>
            <div id="lastCommand" class="status-item">
                Último comando: Ninguno
            </div>
            <div id="connectionStatus" class="status-item">
                ESP32: Desconectado
            </div>
        </div>
    </div>

    <script>
        let currentStatus = {
            esp32_ip: '192.168.0.115',
            last_command: null,
            evasion_enabled: false,
            gate_open: false
        };

        // Función para enviar comandos
        async function sendCommand(command) {
            try {
                updateStatus(`Enviando comando: ${command}...`, 'info');
                
                const response = await fetch('/send_command', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({ command: command })
                });
                
                const data = await response.json();
                
                if (data.status === 'success') {
                    updateStatus(data.message, 'success');
                    updateLastCommand(data.last_command);
                    
                    // Actualizar estado local
                    if (data.evasion_enabled !== undefined) {
                        currentStatus.evasion_enabled = data.evasion_enabled;
                        document.getElementById('evasionToggle').checked = data.evasion_enabled;
                    }
                    if (data.gate_open !== undefined) {
                        currentStatus.gate_open = data.gate_open;
                    }
                } else {
                    updateStatus(`Error: ${data.message}`, 'error');
                }
                
            } catch (error) {
                updateStatus(`Error de conexión: ${error.message}`, 'error');
            }
        }

        // Función para alternar evasión
        async function toggleEvasion() {
            const isEnabled = document.getElementById('evasionToggle').checked;
            const command = isEnabled ? 'EVASION_ON' : 'EVASION_OFF';
            await sendCommand(command);
        }

        // Función para actualizar IP
        async function updateIP() {
            const newIP = document.getElementById('ipInput').value.trim();
            
            if (!newIP) {
                updateStatus('Error: IP no puede estar vacía', 'error');
                return;
            }
            
            try {
                const response = await fetch('/update_ip', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({ ip: newIP })
                });
                
                const data = await response.json();
                
                if (data.status === 'success') {
                    updateStatus(data.message, 'success');
                    currentStatus.esp32_ip = data.new_ip;
                    updateConnectionStatus();
                } else {
                    updateStatus(`Error: ${data.message}`, 'error');
                }
                
            } catch (error) {
                updateStatus(`Error actualizando IP: ${error.message}`, 'error');
            }
        }

        // Funciones de utilidad para UI
        function updateStatus(message, type = 'info') {
            const statusEl = document.getElementById('status');
            statusEl.textContent = message;
            statusEl.className = `status-item status-${type}`;
        }

        function updateLastCommand(command) {
            const lastCommandEl = document.getElementById('lastCommand');
            lastCommandEl.textContent = `Último comando: ${command || 'Ninguno'}`;
            currentStatus.last_command = command;
        }

        function updateConnectionStatus() {
            const connectionEl = document.getElementById('connectionStatus');
            connectionEl.textContent = `ESP32: ${currentStatus.esp32_ip}:80`;
        }

        // Cargar estado inicial
        async function loadInitialStatus() {
            try {
                const response = await fetch('/get_status');
                const data = await response.json();
                
                currentStatus = data;
                document.getElementById('ipInput').value = data.esp32_ip;
                document.getElementById('evasionToggle').checked = data.evasion_enabled;
                updateLastCommand(data.last_command);
                updateConnectionStatus();
                updateStatus('Interfaz cargada correctamente', 'success');
                
            } catch (error) {
                updateStatus('Error cargando estado inicial', 'error');
            }
        }

        // Controles de teclado
        document.addEventListener('keydown', function(event) {
            switch(event.key.toLowerCase()) {
                case 'w':
                case 'arrowup':
                    event.preventDefault();
                    sendCommand('FORWARD');
                    break;
                case 'a':
                case 'arrowleft':
                    event.preventDefault();
                    sendCommand('LEFT');
                    break;
                case 'd':
                case 'arrowright':
                    event.preventDefault();
                    sendCommand('RIGHT');
                    break;
                case 's':
                case 'arrowdown':
                case ' ':
                    event.preventDefault();
                    sendCommand('STOP');
                    break;
                case 'c':
                    event.preventDefault();
                    sendCommand('COLLECT');
                    break;
                case 'p':
                    event.preventDefault();
                    sendCommand('PUSH');
                    break;
                case 'b':
                    event.preventDefault();
                    sendCommand('BACKWARD');
                    break;
            }
        });

        // Cargar estado al inicializar
        window.onload = loadInitialStatus;
    </script>
</body>
</html>
"""

def main():
    """Función principal para ejecutar la interfaz"""
    import argparse
    
    parser = argparse.ArgumentParser(description="Interfaz de control para robot recolector de basura")
    parser.add_argument("--esp32-ip", default="192.168.0.115", help="IP del ESP32 (default: 192.168.0.115)")
    parser.add_argument("--port", type=int, default=5000, help="Puerto para la interfaz web (default: 5000)")
    parser.add_argument("--host", default="0.0.0.0", help="Host para la interfaz web (default: 0.0.0.0)")
    parser.add_argument("--debug", action="store_true", help="Ejecutar en modo debug")
    
    args = parser.parse_args()
    
    # Crear e iniciar la interfaz
    interface = RobotControlInterface(esp32_ip=args.esp32_ip)
    
    print("=" * 60)
    print("🤖 INTERFAZ DE CONTROL ROBOT RECOLECTOR")
    print("=" * 60)
    print(f"ESP32 IP: {args.esp32_ip}")
    print(f"Interfaz web: http://{args.host}:{args.port}")
    print("\nControles de teclado disponibles:")
    print("  W/↑ : Adelante    A/← : Izquierda")
    print("  D/→ : Derecha     S/↓/Espacio : Stop")
    print("  C   : Recolectar  P : Expulsar")
    print("\nPresiona Ctrl+C para detener")
    print("=" * 60)
    
    try:
        interface.run(host=args.host, port=args.port, debug=args.debug)
    except KeyboardInterrupt:
        print("\n\n🛑 Interfaz detenida por el usuario")
    except Exception as e:
        print(f"\n❌ Error ejecutando interfaz: {e}")

if __name__ == "__main__":
    main()