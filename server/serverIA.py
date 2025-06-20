import cv2
import numpy as np
from ultralytics import YOLO
import requests
import socket
import json
import time
import threading
from collections import deque
import math

class WasteDetectionSystem:
    def __init__(self, phone_ip="192.168.1.13", phone_port=8080, esp32_ip="192.168.1.101", esp32_port=80):
        """
        Sistema de detección de desechos para robot recolector
        
        Args:
            phone_ip: IP del teléfono con IP Webcam
            phone_port: Puerto de IP Webcam (por defecto 8080)
            esp32_ip: IP del ESP32
            esp32_port: Puerto del ESP32 (por defecto 80)
        """
        self.phone_ip = phone_ip
        self.phone_port = phone_port
        self.esp32_ip = esp32_ip
        self.esp32_port = esp32_port
        
        # URL del stream de video del teléfono
        self.video_url = f"http://{phone_ip}:{phone_port}/video"
        
        # Cargar modelo YOLO pre-entrenado
        print("Cargando modelo YOLO...")
        self.model = YOLO('last.pt')
        print(self.model.names)
        # Clases específicas que nos interesan (índices de COCO dataset)
        # Estas son las clases más relevantes para desechos
        self.target_classes = {
            'can': 2,                # latas
            'plastic_bag': 10,       # bolsa plástica
            'scrap_paper': 17,       # papel de desecho
            'reuseable_paper': 16,   # papel reutilizable
            'plastic_cup': 14,       # vaso plástico
            'snack_bag': 19,         # bolsas de snacks
        }
        
        # Configuración de detección
        self.confidence_threshold = 0.1
        self.frame_width = 640
        self.frame_height = 480
        
        # Cola para suavizar las detecciones
        self.detection_history = deque(maxlen=5)
        
        # Variables de control
        self.running = False
        self.current_target = None
        
        print("Sistema inicializado correctamente")
    
    def connect_to_video_stream(self):
        """Conecta al stream de video del teléfono"""
        try:
            self.cap = cv2.VideoCapture(self.video_url)
            if not self.cap.isOpened():
                raise Exception("No se pudo conectar al stream de video")
            
            # Configurar resolución
            self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, self.frame_width)
            self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, self.frame_height)
            
            print(f"Conectado al stream de video: {self.video_url}")
            return True
        except Exception as e:
            print(f"Error conectando al video: {e}")
            return False
    
    def send_command_to_esp32(self, command):
        """Envía comando al ESP32"""
        print(f"Comando enviado: {command}")
        # try:
        #     url = f"http://{self.esp32_ip}:{self.esp32_port}/{command}"
        #     response = requests.get(url, timeout=2)
        #     if response.status_code == 200:
        #         print(f"Comando enviado: {command}")
        #         return True
        #     else:
        #         print(f"Error enviando comando: {response.status_code}")
        #         return False
        # except Exception as e:
        #     print(f"Error comunicando con ESP32: {e}")
        #     return False
    
    def detect_waste(self, frame):
        """Detecta desechos en el frame"""
        # Ejecutar detección YOLO
        results = self.model(frame, conf=self.confidence_threshold)
        
        detections = []
        
        for result in results:
            boxes = result.boxes
            if boxes is not None:
                for box in boxes:
                    # Obtener coordenadas y confianza
                    x1, y1, x2, y2 = box.xyxy[0].cpu().numpy()
                    confidence = box.conf[0].cpu().numpy()
                    class_id = int(box.cls[0].cpu().numpy())
                    
                    # Verificar si es una clase objetivo
                    class_name = self.model.names[class_id]
                    
                    # Para este ejemplo, detectamos cualquier objeto como potencial desecho
                    # En una implementación real, podrías entrenar un modelo específico
                    if confidence > self.confidence_threshold:
                        center_x = int((x1 + x2) / 2)
                        center_y = int((y1 + y2) / 2)
                        width = int(x2 - x1)
                        height = int(y2 - y1)
                        
                        detection = {
                            'class_name': class_name,
                            'confidence': confidence,
                            'center_x': center_x,
                            'center_y': center_y,
                            'width': width,
                            'height': height,
                            'bbox': (int(x1), int(y1), int(x2), int(y2))
                        }
                        detections.append(detection)
        
        return detections
    
    def calculate_movement_direction(self, detections):
        """Calcula la dirección de movimiento hacia el desecho más cercano"""
        if not detections:
            return None
        
        # Encontrar el desecho más grande (más cercano probablemente)
        largest_detection = max(detections, key=lambda x: x['width'] * x['height'])
        
        # Calcular posición relativa respecto al centro del frame
        frame_center_x = self.frame_width // 2
        frame_center_y = self.frame_height // 2
        
        target_x = largest_detection['center_x']
        target_y = largest_detection['center_y']
        
        # Calcular diferencias
        dx = target_x - frame_center_x
        dy = target_y - frame_center_y
        
        # Definir zona muerta para evitar movimientos innecesarios
        dead_zone = 50
        
        command = None
        
        # Determinar comando basado en la posición del objeto
        if abs(dx) > dead_zone:
            if dx > 0:
                command = "turn_right"
            else:
                command = "turn_left"
        elif largest_detection['width'] * largest_detection['height'] < 10000:  # Si el objeto es pequeño (lejano)
            command = "move_forward"
        else:
            command = "stop"  # Objeto cerca, detener para recolectar
        
        return {
            'command': command,
            'target_info': largest_detection
        }
    
    def draw_detections(self, frame, detections):
        """Dibuja las detecciones en el frame"""
        for detection in detections:
            x1, y1, x2, y2 = detection['bbox']
            
            # Dibujar rectángulo
            cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 255, 0), 2)
            
            # Dibujar etiqueta
            label = f"{detection['class_name']}: {detection['confidence']:.2f}"
            cv2.putText(frame, label, (x1, y1-10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 2)
            
            # Dibujar punto central
            cv2.circle(frame, (detection['center_x'], detection['center_y']), 5, (0, 0, 255), -1)
        
        # Dibujar líneas de referencia
        cv2.line(frame, (self.frame_width//2, 0), (self.frame_width//2, self.frame_height), (255, 0, 0), 1)
        cv2.line(frame, (0, self.frame_height//2), (self.frame_width, self.frame_height//2), (255, 0, 0), 1)
        
        return frame
    
    def run(self):
        """Ejecuta el sistema principal"""
        if not self.connect_to_video_stream():
            return
        
        self.running = True
        print("Iniciando detección de desechos...")
        
        try:
            while self.running:
                ret, frame = self.cap.read()
                if not ret:
                    print("Error leyendo frame")
                    break
                
                # Detectar desechos
                detections = self.detect_waste(frame)
                
                # Agregar a historial para suavizar
                self.detection_history.append(detections)
                
                # Calcular movimiento solo si tenemos suficiente historial
                if len(self.detection_history) >= 3:
                    # Usar las detecciones más recientes
                    recent_detections = self.detection_history[-1]
                    
                    if recent_detections:
                        movement = self.calculate_movement_direction(recent_detections)
                        
                        if movement:
                            # Enviar comando al ESP32
                            self.send_command_to_esp32(movement['command'])
                            
                            # Mostrar información
                            target = movement['target_info']
                            print(f"Objetivo: {target['class_name']} - Comando: {movement['command']}")
                    else:
                        # No hay detecciones, buscar
                        self.send_command_to_esp32("search")
                
                # Dibujar detecciones en el frame
                frame_with_detections = self.draw_detections(frame.copy(), detections)
                
                # Mostrar video con detecciones
                cv2.imshow('Robot Waste Detection', frame_with_detections)
                
                # Control de salida
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    break
                
                # Pequeña pausa para no sobrecargar
                time.sleep(0.1)
        
        except KeyboardInterrupt:
            print("Deteniendo sistema...")
        
        finally:
            self.stop()
    
    def stop(self):
        """Detiene el sistema"""
        self.running = False
        if hasattr(self, 'cap'):
            self.cap.release()
        cv2.destroyAllWindows()
        
        # Enviar comando de parar al robot
        self.send_command_to_esp32("stop")
        print("Sistema detenido")

def main():
    """Función principal"""
    print("=== Robot Recolector de Desechos ===")
    print("Configurando sistema...")
    
    # Configurar IPs (cambiar según tu red)
    PHONE_IP = "192.168.1.13"  # IP de tu teléfono
    ESP32_IP = "192.168.1.101"  # IP de tu ESP32
    
    # Crear e inicializar sistema
    waste_detector = WasteDetectionSystem(
        phone_ip=PHONE_IP,
        phone_port=8080,
        esp32_ip=ESP32_IP,
        esp32_port=80
    )
    
    print("Presiona 'q' para salir")
    print("Comandos que se enviarán al ESP32:")
    print("- move_forward: avanzar")
    print("- turn_left: girar izquierda")
    print("- turn_right: girar derecha")
    print("- stop: detener")
    print("- search: buscar (rotar)")
    
    # Ejecutar sistema
    waste_detector.run()

if __name__ == "__main__":
    main()