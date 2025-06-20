# Robot Recolector de Desechos - Guía de Instalación

## 📋 Requisitos del Sistema

### Hardware Necesario:
- **ESP32** (con WiFi)
- **Puente H L298N** (para control de motores)
- **2 Motores DC** con ruedas
- **Teléfono Android** con cámara
- **PC/Laptop** con Python
- **Router WiFi** para conectar todos los dispositivos
- **Cables y protoboard** para conexiones

### Software Necesario:
- **Python 3.8+** en la PC
- **Arduino IDE** para programar el ESP32
- **IP Webcam** app en Android

## 🔧 Instalación del Software

### 1. Configuración de Python (PC)

```bash
# Crear entorno virtual
python -m venv robot_env
source robot_env/bin/activate  # En Windows: robot_env\Scripts\activate

# Instalar dependencias
pip install ultralytics opencv-python requests numpy

# Descargar modelo YOLO (se hace automático al ejecutar)
# El modelo yolov8n.pt se descarga automáticamente
```

### 2. Configuración del ESP32

**Instalar librerías en Arduino IDE:**
- ESP32 Board Package
- ArduinoJson library
- WiFi library (incluida con ESP32)

**Conexiones del ESP32 con L298N:**
```
ESP32    →    L298N
GPIO2    →    IN1 (Motor Izq A)
GPIO4    →    IN2 (Motor Izq B)  
GPIO5    →    ENA (PWM Motor Izq)
GPIO18   →    IN3 (Motor Der A)
GPIO19   →    IN4 (Motor Der B)
GPIO21   →    ENB (PWM Motor Der)
VIN      →    +12V (Fuente externa)
GND      →    GND común
```

**Conexiones L298N con Motores:**
```
L298N    →    Motores
OUT1     →    Motor Izquierdo +
OUT2     →    Motor Izquierdo -
OUT3     →    Motor Derecho +
OUT4     →    Motor Derecho -
```

### 3. Configuración del Teléfono Android

1. **Instalar IP Webcam** desde Google Play Store
2. **Configurar IP Webcam:**
   - Abrir la app
   - Ir a configuración
   - **Video resolution**: 640x480 (para mejor rendimiento)
   - **Quality**: 50-70%
   - **FPS limit**: 15 fps
   - Anotar la IP que muestra la app

## ⚙️ Configuración de Red

### 1. Configurar IPs Estáticas (Recomendado)

**En tu router WiFi:**
- **ESP32**: 192.168.1.101
- **Teléfono**: 192.168.1.100  
- **PC**: 192.168.1.102

### 2. Modificar el Código

**En el código Python** (`main()` function):
```python
PHONE_IP = "192.168.1.100"  # IP de tu teléfono
ESP32_IP = "192.168.1.101"  # IP de tu ESP32
```

**En el código ESP32:**
```cpp
const char* ssid = "TU_WIFI_SSID";         // Nombre de tu WiFi
const char* password = "TU_WIFI_PASSWORD"; // Contraseña de tu WiFi
```

## 🚀 Ejecución del Sistema

### 1. Preparar el ESP32
1. Cargar el código en el ESP32
2. Abrir el monitor serie para ver la IP asignada
3. Verificar que se conecte al WiFi

### 2. Preparar el Teléfono
1. Conectar a la misma red WiFi
2. Abrir IP Webcam
3. Presionar "Start server"
4. Anotar la URL que aparece (ej: http://192.168.1.100:8080)

### 3. Ejecutar el Sistema
1. **Activar entorno Python:**
   ```bash
   source robot_env/bin/activate  # En Windows: robot_env\Scripts\activate
   ```

2. **Ejecutar el programa principal:**
   ```bash
   python waste_detection_system.py
   ```

3. **Verificar conexiones:**
   - El programa debería mostrar "Conectado al stream de video"
   - Debería aparecer una ventana con el video del teléfono
   - Los comandos se enviarán automáticamente al ESP32

## 🎯 Calibración y Ajustes

### 1. Ajustar Detección
En el código Python, puedes modificar:
```python
# Umbral de confianza (0.1 = menos estricto, 0.9 = más estricto)
self.confidence_threshold = 0.5

# Zona muerta para evitar movimientos innecesarios
dead_zone = 50  # pixeles

# Tamaño mínimo para considerar objeto "cerca"
min_size_for_stop = 10000  # pixeles²
```

### 2. Ajustar Velocidades del Robot
Puedes cambiar las velocidades enviando un POST al ESP32:
```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"velocidad_base":180,"velocidad_giro":140}' \
  http://192.168.1.101/config
```

### 3. Monitorear Estado del Robot
```bash
curl http://192.168.1.101/status
```

## 🔍 Mejoras para Detección de Envolturas y Servilletas

### Opción 1: Entrenar Modelo Personalizado
Para mayor precisión con envolturas y servilletas específicas:

```python
# Instalar roboflow para datasets personalizados
pip install roboflow

# Código para entrenar modelo personalizado
from roboflow import Roboflow
from ultralytics import YOLO

def train_custom_model():
    # Descargar dataset personalizado (requiere crear cuenta en Roboflow)
    rf = Roboflow(api_key="TU_API_KEY")
    project = rf.workspace("workspace").project("waste-detection")
    dataset = project.version(1).download("yolov8")
    
    # Entrenar modelo personalizado
    model = YOLO('yolov8n.pt')
    model.train(data=f'{dataset.location}/data.yaml', epochs=50)
    
    return model
```

### Opción 2: Filtros Adicionales por Color
```python
def detect_by_color(self, frame):
    """Detecta objetos por color típico de envolturas"""
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    
    # Rangos de color para envolturas comunes
    ranges = [
        # Plásticos brillantes (colores saturados)
        ([0, 50, 50], [10, 255, 255]),    # Rojos
        ([20, 50, 50], [30, 255, 255]),   # Amarillos
        ([100, 50, 50], [130, 255, 255]), # Azules
    ]
    
    mask_combined = np.zeros(hsv.shape[:2], dtype=np.uint8)
    
    for lower, upper in ranges:
        mask = cv2.inRange(hsv, np.array(lower), np.array(upper))
        mask_combined = cv2.bitwise_or(mask_combined, mask)
    
    # Encontrar contornos
    contours, _ = cv2.findContours(mask_combined, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    
    color_detections = []
    for contour in contours:
        area = cv2.contourArea(contour)
        if area > 500:  # Filtrar objetos muy pequeños
            x, y, w, h = cv2.boundingRect(contour)
            color_detections.append({
                'class_name': 'wrapper_by_color',
                'confidence': 0.8,
                'center_x': x + w//2,
                'center_y': y + h//2,
                'width': w,
                'height': h,
                'bbox': (x, y, x+w, y+h)
            })
    
    return color_detections
```

## 🐛 Resolución de Problemas

### Problema: No se conecta al video del teléfono
**Soluciones:**
1. Verificar que el teléfono y PC están en la misma red
2. Desactivar firewall temporalmente
3. Probar la URL del video en el navegador: `http://IP_TELEFONO:8080/video`
4. Cambiar el puerto en IP Webcam si está ocupado

### Problema: ESP32 no responde a comandos
**Soluciones:**
1. Verificar IP del ESP32 en monitor serie
2. Probar comandos manualmente: `http://IP_ESP32/status`
3. Revisar conexiones de los motores
4. Verificar que la fuente de alimentación sea suficiente (12V, 2A mínimo)

### Problema: Detección imprecisa
**Soluciones:**
1. Ajustar `confidence_threshold` (bajar para más detecciones)
2. Mejorar iluminación del área
3. Limpiar lente de la cámara
4. Entrenar modelo personalizado con tus tipos específicos de desechos

### Problema: Robot se mueve erráticamente
**Soluciones:**
1. Calibrar velocidades con `/config`
2. Ajustar `dead_zone` para reducir sensibilidad
3. Verificar que los motores estén bien balanceados
4. Revisar conexiones PWM del puente H

## 📊 Monitoreo y Logs

### Ver logs en tiempo real:
```bash
# En el ESP32 (Monitor Serie)
# Muestra: estado actual, comandos recibidos, errores de conexión

# En Python
# Muestra: detecciones, comandos enviados, errores de video
```

### Estadísticas del sistema:
```bash
# Obtener estado completo del ESP32
curl http://192.168.1.101/status

# Respuesta ejemplo:
{
  "estado": "AVANZANDO",
  "velocidad_base": 150,
  "velocidad_giro": 120,
  "wifi_signal": -45,
  "uptime": 125000,
  "ip": "192.168.1.101"
}
```

## 🔄 Actualizaciones Futuras

### Funcionalidades Adicionales:
1. **Control remoto manual** vía web
2. **Recolección automática** con servo/brazo mecánico  
3. **Mapeo del área** con SLAM básico
4. **Notificaciones** vía Telegram/WhatsApp
5. **Base de carga automática**

### Sensores Adicionales:
1. **Sensor ultrasónico** para evitar obstáculos
2. **Giroscopio** para navegación más precisa
3. **Sensor de carga** para batería
4. **LEDs indicadores** de estado

## 📝 Comandos de Prueba

### Probar ESP32 manualmente:
```bash
# Avanzar
curl http://192.168.1.101/move_forward

# Girar derecha  
curl http://192.168.1.101/turn_right

# Detener
curl http://192.168.1.101/stop

# Estado
curl http://192.168.1.101/status
```

### Probar video del teléfono:
```bash
# En navegador web
http://IP_TELEFONO:8080

# Stream directo de video
http://IP_TELEFONO:8080/video
```

## ⚠️ Notas de Seguridad

1. **Supervisión:** Siempre supervisa el robot durante las pruebas
2. **Área de prueba:** Usa un área cerrada y sin obstáculos frágiles
3. **Alimentación:** Desconecta la batería cuando no uses el robot
4. **Cables:** Asegura todos los cables para evitar enredos
5. **Velocidad:** Comienza con velocidades bajas hasta calibrar bien

## 🎉 ¡Listo para Usar!

Una vez configurado todo, tu robot debería:
1. **Detectar** envolturas y desechos en el suelo
2. **Navegar** automáticamente hacia ellos
3. **Detenerse** cuando esté cerca para recolección
4. **Buscar** nuevos objetivos cuando no detecte nada

¡Disfruta tu robot recolector de desechos! 🤖♻️