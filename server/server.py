import cv2
import numpy as np
import requests
import threading
import time
import json
from datetime import datetime

class WasteDetectionSystem:
    def __init__(self, phone_ip="192.168.1.13", esp32_ip="192.168.1.101"):
        """
        Sistema de detección de desechos sólidos
        Args:
            phone_ip: IP del teléfono Android
            esp32_ip: IP del ESP32
        """
        self.phone_ip = phone_ip
        self.esp32_ip = esp32_ip
        self.phone_stream_url = f"http://{phone_ip}:8080/video"
        self.esp32_command_url = f"http://{esp32_ip}/command"
        
        # Variables de control
        self.is_running = False
        self.frame = None
        self.waste_detected = False
        self.waste_position = None
        
        # Configuración de detección
        self.setup_detection_parameters()
        
    def setup_detection_parameters(self):
        """Configura los parámetros para detección de desechos"""
        # Rangos de color para diferentes tipos de desechos
        # Papeles blancos/claros
        self.white_paper_lower = np.array([0, 0, 200])
        self.white_paper_upper = np.array([180, 30, 255])
        
        # Envolturas coloridas (ajustar según necesidad)
        self.colorful_lower = np.array([0, 50, 50])
        self.colorful_upper = np.array([180, 255, 255])
        
        # Parámetros de filtrado
        self.min_contour_area = 500  # Área mínima para considerar un objeto
        self.max_contour_area = 50000  # Área máxima
        
    def connect_to_phone_stream(self):
        """Establece conexión con el stream de video del teléfono"""
        try:
            self.cap = cv2.VideoCapture(self.phone_stream_url)
            if not self.cap.isOpened():
                print(f"Error: No se pudo conectar al stream del teléfono en {self.phone_stream_url}")
                return False
            print("Conexión establecida con el teléfono Android")
            return True
        except Exception as e:
            print(f"Error conectando al teléfono: {e}")
            return False
    
    def detect_waste_objects(self, frame):
        """
        Detecta objetos de desecho en el frame
        Returns: lista de objetos detectados con sus posiciones
        """
        detected_objects = []
        
        # Convertir a HSV para mejor detección de colores
        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
        
        # Crear máscaras para diferentes tipos de desechos
        masks = []
        
        # Máscara para papeles blancos
        white_mask = cv2.inRange(hsv, self.white_paper_lower, self.white_paper_upper)
        masks.append(("papel_blanco", white_mask))
        
        # Máscara para objetos coloridos (envolturas)
        colorful_mask = cv2.inRange(hsv, self.colorful_lower, self.colorful_upper)
        masks.append(("envoltura", colorful_mask))
        
        # Procesar cada máscara
        for waste_type, mask in masks:
            # Aplicar operaciones morfológicas para limpiar la máscara
            kernel = np.ones((5,5), np.uint8)
            mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel)
            mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)
            
            # Encontrar contornos
            contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
            
            for contour in contours:
                area = cv2.contourArea(contour)
                
                # Filtrar por área
                if self.min_contour_area < area < self.max_contour_area:
                    # Calcular centro del objeto
                    M = cv2.moments(contour)
                    if M["m00"] != 0:
                        cx = int(M["m10"] / M["m00"])
                        cy = int(M["m01"] / M["m00"])
                        
                        # Calcular bounding box
                        x, y, w, h = cv2.boundingRect(contour)
                        
                        detected_objects.append({
                            'type': waste_type,
                            'center': (cx, cy),
                            'bbox': (x, y, w, h),
                            'area': area,
                            'contour': contour
                        })
        
        return detected_objects
    
    def calculate_movement_command(self, waste_objects, frame_shape):
        """
        Calcula el comando de movimiento basado en los objetos detectados
        """
        if not waste_objects:
            return "STOP"
        
        frame_height, frame_width = frame_shape[:2]
        frame_center_x = frame_width // 2
        frame_center_y = frame_height // 2
        
        # Encontrar el objeto más cercano (más grande en el frame inferior)
        closest_object = None
        max_priority = 0
        
        for obj in waste_objects:
            cx, cy = obj['center']
            area = obj['area']
            
            # Priorizar objetos en la parte inferior del frame (más cercanos)
            distance_from_bottom = frame_height - cy
            priority = area * (1 + (frame_height - distance_from_bottom) / frame_height)
            
            if priority > max_priority:
                max_priority = priority
                closest_object = obj
        
        if not closest_object:
            return "STOP"
        
        cx, cy = closest_object['center']
        
        # Calcular comando basado en la posición del objeto
        tolerance_x = 50  # Tolerancia horizontal
        tolerance_y = 100  # Tolerancia para considerar "cerca"
        
        # Si el objeto está muy cerca (parte inferior del frame)
        if cy > frame_height - tolerance_y:
            return "COLLECT"  # Activar mecanismo de recolección
        
        # Determinar dirección horizontal
        if cx < frame_center_x - tolerance_x:
            return "LEFT"
        elif cx > frame_center_x + tolerance_x:
            return "RIGHT"
        else:
            return "FORWARD"
    
    def send_command_to_esp32(self, command):
        """Envía comando al ESP32"""
        try:
            payload = {"command": command, "timestamp": datetime.now().isoformat()}
            response = requests.post(self.esp32_command_url, 
                                   json=payload, 
                                   timeout=2)
            if response.status_code == 200:
                print(f"Comando enviado: {command}")
                return True
            else:
                print(f"Error enviando comando: {response.status_code}")
                return False
        except Exception as e:
            print(f"Error conectando con ESP32: {e}")
            return False
    
    def draw_detection_info(self, frame, waste_objects):
        """Dibuja información de detección en el frame"""
        for obj in waste_objects:
            # Dibujar bounding box
            x, y, w, h = obj['bbox']
            cv2.rectangle(frame, (x, y), (x + w, y + h), (0, 255, 0), 2)
            
            # Dibujar centro
            cx, cy = obj['center']
            cv2.circle(frame, (cx, cy), 5, (0, 0, 255), -1)
            
            # Etiqueta
            label = f"{obj['type']}: {obj['area']:.0f}px²"
            cv2.putText(frame, label, (x, y-10), 
                       cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)
        
        # Dibujar líneas de referencia
        h, w = frame.shape[:2]
        cv2.line(frame, (w//2, 0), (w//2, h), (255, 0, 0), 1)  # Línea central vertical
        cv2.line(frame, (0, h-100), (w, h-100), (255, 0, 0), 1)  # Línea de "cerca"
        
        return frame
    
    def process_video_stream(self):
        """Procesa el stream de video en tiempo real"""
        last_command_time = 0
        command_interval = 1.0  # Enviar comandos cada segundo
        
        while self.is_running:
            ret, frame = self.cap.read()
            if not ret:
                print("Error leyendo frame del stream")
                continue
            
            # Detectar objetos de desecho
            waste_objects = self.detect_waste_objects(frame)
            
            # Calcular comando de movimiento
            command = self.calculate_movement_command(waste_objects, frame.shape)
            
            # Enviar comando al ESP32 (con limitación de frecuencia)
            current_time = time.time()
            if current_time - last_command_time > command_interval:
                self.send_command_to_esp32(command)
                last_command_time = current_time
            
            # Dibujar información de detección
            frame_with_info = self.draw_detection_info(frame, waste_objects)
            
            # Mostrar frame procesado
            cv2.imshow('Robot Recolector - Detección de Desechos', frame_with_info)
            
            # Información en consola
            if waste_objects:
                print(f"Objetos detectados: {len(waste_objects)} | Comando: {command}")
            
            # Salir con 'q'
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break
    
    def start_detection(self):
        """Inicia el sistema de detección"""
        print("Iniciando sistema de detección de desechos...")
        
        if not self.connect_to_phone_stream():
            return False
        
        self.is_running = True
        
        try:
            self.process_video_stream()
        except KeyboardInterrupt:
            print("\nDeteniendo sistema...")
        finally:
            self.stop_detection()
        
        return True
    
    def stop_detection(self):
        """Detiene el sistema de detección"""
        self.is_running = False
        if hasattr(self, 'cap'):
            self.cap.release()
        cv2.destroyAllWindows()
        print("Sistema detenido")

# Función principal
def main():
    # Configurar IPs de dispositivos
    PHONE_IP = "192.168.1.13"  # Cambiar por la IP de tu teléfono
    ESP32_IP = "192.168.1.101"  # Cambiar por la IP de tu ESP32
    
    # Crear e iniciar el sistema
    detector = WasteDetectionSystem(PHONE_IP, ESP32_IP)
    detector.start_detection()

if __name__ == "__main__":
    main()