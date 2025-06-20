# Configuración del Sistema de Detección de Desechos

## 📱 Configuración del Teléfono Android

### Paso 1: Instalar App de Streaming
Necesitas una aplicación que permita transmitir video por Wi-Fi. Recomendaciones:

1. **IP Webcam** (Recomendada)
   - Descargar desde Google Play Store
   - Configurar resolución: 640x480 o 800x600 (para mejor rendimiento)
   - Puerto: 8080 (predeterminado)
   - Calidad: Media (para reducir latencia)

2. **DroidCam** (Alternativa)
   - También disponible en Play Store
   - Configuración similar

### Paso 2: Configurar la Red Wi-Fi
1. Conecta tu teléfono a la misma red Wi-Fi que tu PC
2. Anota la IP de tu teléfono:
   - Ve a Configuración → Wi-Fi → Información de red
   - O ejecuta IP Webcam y verás la IP mostrada
3. La URL del stream será: `http://[IP_DEL_TELEFONO]:8080/video`

### Paso 3: Posicionamiento del Teléfono
- Montar el teléfono en la parte frontal del robot
- Ángulo hacia abajo de 15-30 grados para ver el suelo
- Altura recomendada: 20-40 cm del suelo
- Asegurar que la cámara esté estable

## 💻 Configuración de la PC

### Dependencias de Python
```bash
pip install opencv-python numpy requests
```

### Configuración del Código
1. Editar las IPs en el archivo principal:
   ```python
   PHONE_IP = "192.168.1.XXX"  # IP de tu teléfono
   ESP32_IP = "192.168.1.XXX"  # IP de tu ESP32
   ```

2. Ajustar parámetros de detección según tu entorno:
   - `min_contour_area`: Área mínima para detectar objetos
   - `max_contour_area`: Área máxima
   - Rangos de color HSV según los desechos específicos

## 🔧 Configuración del ESP32

*Pendiente*

## ⚙️ Calibración del Sistema

### 1. Calibración de Colores
- Ejecutar el sistema en modo de prueba
- Observar qué objetos se detectan correctamente
- Ajustar rangos HSV en `setup_detection_parameters()`

### 2. Ajuste de Sensibilidad
- Modificar `min_contour_area` y `max_contour_area`
- Probar en diferentes condiciones de iluminación
- Ajustar tolerancias de movimiento

### 3. Optimización de Red
- Reducir resolución del video si hay latencia
- Ajustar `command_interval` para frecuencia de comandos
- Verificar estabilidad de la conexión WiFi

## 🚀 Ejecución del Sistema

1. **Iniciar stream en el teléfono**: Abrir IP Webcam y presionar "Start Server"
2. **Verificar conexión**: Desde PC, abrir navegador y ir a `http://[IP_TELEFONO]:8080`
3. **Ejecutar código Python**: `python waste_detection_system.py`
4. **Monitorear**: Observar la ventana de detección y logs en consola

## 🛠️ Solución de Problemas

### Problemas Comunes:
- **No se conecta al stream**: Verificar IP y que ambos dispositivos estén en la misma red
- **Detección errónea**: Ajustar rangos de color HSV
- **Latencia alta**: Reducir resolución del video
- **Comandos no llegan al ESP32**: Verificar IP del ESP32 y conexión WiFi

### Comandos de Depuración:
- Usar `ping [IP_DISPOSITIVO]` para verificar conectividad
- Revisar logs en consola para errores específicos
- Probar URLs manualmente en navegador
