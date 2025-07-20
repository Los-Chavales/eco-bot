from flask import Flask, render_template, request, jsonify
import requests
import logging

# --- CONFIGURACIÓN ---
ESP32_IP = "192.168.0.115"  # Reemplaza con la IP de tu ESP32
ESP32_URL = f"http://{ESP32_IP}/command"

# Configuración de logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(message)s')

app = Flask(__name__)

# Función para enviar el comando al ESP32
def send_command_to_esp32(command):
    """Envía un comando al ESP32 vía HTTP POST."""
    try:
        payload = {"command": command}
        response = requests.post(ESP32_URL, json=payload, timeout=3)
        if response.status_code == 200:
            logging.info(f"Comando '{command}' enviado correctamente al ESP32.")
            return True, "Comando enviado"
        else:
            logging.error(f"Error en ESP32: {response.status_code}")
            return False, f"Error en ESP32: {response.status_code}"
    except requests.exceptions.RequestException as e:
        logging.error(f"Error de conexión con el ESP32: {e}")
        return False, "Error de conexión con el ESP32"

# Ruta para la página principal
@app.route('/')
def index():
    """Sirve el archivo index.html."""
    return render_template('index.html')

# Ruta para recibir comandos desde la interfaz web
@app.route('/send_command', methods=['POST'])
def handle_command():
    """Recibe un comando del navegador y lo reenvía al ESP32."""
    data = request.get_json()
    command = data.get('command')
    
    if not command:
        return jsonify({"status": "error", "message": "Comando no especificado."}), 400
    
    success, message = send_command_to_esp32(command)
    
    if success:
        return jsonify({"status": "ok", "message": f"Comando '{command}' enviado."})
    else:
        return jsonify({"status": "error", "message": message}), 500

if __name__ == '__main__':
    # Inicia el servidor en la IP de tu PC, visible en tu red local
    app.run(host='0.0.0.0', port=5000, debug=True)