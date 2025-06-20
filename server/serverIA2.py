import cv2
import numpy as np
from ultralytics import YOLO
import requests
import time
from collections import deque
from datetime import datetime

class WasteDetectionSystem:
    VALID_COMMANDS = {"FORWARD", "LEFT", "RIGHT", "STOP", "COLLECT"}

    def __init__(self, phone_ip="192.168.0.101", phone_port=8080, esp32_ip="192.168.1.101", esp32_port=80):
        self.phone_ip = phone_ip
        self.phone_port = phone_port
        self.esp32_ip = esp32_ip
        self.esp32_port = esp32_port

        self.video_url = f"http://{phone_ip}:{phone_port}/video"
        self.esp32_command_url = f"http://{esp32_ip}/command"

        print("Cargando modelo YOLO...")
        self.model = YOLO('last.pt')
        print(self.model.names)

        self.confidence_threshold = 0.3
        self.frame_width = 640
        self.frame_height = 480

        self.detection_history = deque(maxlen=5)
        self.running = False
        self._last_command = None

        print("Sistema inicializado correctamente")

    def connect_to_video_stream(self):
        try:
            self.cap = cv2.VideoCapture(self.video_url)
            if not self.cap.isOpened():
                raise Exception("No se pudo conectar al stream de video")
            self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, self.frame_width)
            self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, self.frame_height)
            print(f"Conectado al stream de video: {self.video_url}")
            return True
        except Exception as e:
            print(f"Error conectando al video: {e}")
            return False

    def send_command_to_esp32(self, command):
        if self._last_command == command:
            return
        if command not in self.VALID_COMMANDS:
            command = "STOP"
        self._last_command = command
        print(f"Comando enviado: {command}")
        try:
            payload = {"command": command, "timestamp": datetime.now().isoformat()}
            response = requests.post(self.esp32_command_url, json=payload, timeout=2)
            if response.status_code == 200:
                print(f"Comando {command} enviado correctamente")
            else:
                print(f"Error enviando comando: {response.status_code}")
        except Exception as e:
            print(f"Error comunicando con ESP32: {e}")

    def detect_waste(self, frame):
        if frame.shape[1] != self.frame_width or frame.shape[0] != self.frame_height:
            frame = cv2.resize(frame, (self.frame_width, self.frame_height))
        results = self.model(frame, conf=self.confidence_threshold)
        detections = []
        for result in results:
            boxes = result.boxes
            if boxes is not None:
                for box in boxes:
                    x1, y1, x2, y2 = box.xyxy[0].cpu().numpy()
                    confidence = box.conf[0].cpu().numpy()
                    class_id = int(box.cls[0].cpu().numpy())
                    class_name = self.model.names[class_id]
                    if confidence > self.confidence_threshold:
                        center_x = int((x1 + x2) / 2)
                        center_y = int((y1 + y2) / 2)
                        width = int(x2 - x1)
                        height = int(y2 - y1)
                        area = width * height
                        detection = {
                            'class_name': class_name,
                            'confidence': confidence,
                            'center_x': center_x,
                            'center_y': center_y,
                            'width': width,
                            'height': height,
                            'area': area,
                            'bbox': (int(x1), int(y1), int(x2), int(y2))
                        }
                        detections.append(detection)
        return detections

    def calculate_movement_command(self, detections):
        if not detections:
            return "STOP"
        # Prioriza área y cercanía al fondo (más robusto)
        best_detection = max(
            detections,
            key=lambda d: d['area'] * (1 + (self.frame_height - d['center_y']) / self.frame_height)
        )
        cx, cy = best_detection['center_x'], best_detection['center_y']
        frame_center_x = self.frame_width // 2
        tolerance_x = 50
        tolerance_y = 100
        if cy > self.frame_height - tolerance_y:
            return "COLLECT"
        if cx < frame_center_x - tolerance_x:
            return "LEFT"
        elif cx > frame_center_x + tolerance_x:
            return "RIGHT"
        else:
            return "FORWARD"

    def draw_detections(self, frame, detections):
        for detection in detections:
            x1, y1, x2, y2 = detection['bbox']
            cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 255, 0), 2)
            label = f"{detection['class_name']}: {detection['confidence']:.2f}"
            cv2.putText(frame, label, (x1, y1 - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 2)
            cv2.circle(frame, (detection['center_x'], detection['center_y']), 5, (0, 0, 255), -1)
        h, w = frame.shape[:2]
        cv2.line(frame, (w // 2, 0), (w // 2, h), (255, 0, 0), 1)
        cv2.line(frame, (0, h - 100), (w, h - 100), (255, 0, 0), 1)
        return frame

    def run(self):
        if not self.connect_to_video_stream():
            return
        self.running = True
        print("Iniciando detección de desechos...")

        try:
            last_command_time = 0
            command_interval = 1.0  # segundos entre comandos
            while self.running:
                ret, frame = self.cap.read()
                if not ret:
                    print("Error leyendo frame")
                    break
                detections = self.detect_waste(frame)
                command = self.calculate_movement_command(detections)
                current_time = time.time()
                if current_time - last_command_time > command_interval:
                    self.send_command_to_esp32(command)
                    last_command_time = current_time

                frame_with_detections = self.draw_detections(frame.copy(), detections)
                cv2.imshow('Robot Waste Detection', frame_with_detections)

                if cv2.waitKey(1) & 0xFF == ord('q'):
                    break
                time.sleep(0.05)
        except KeyboardInterrupt:
            print("Deteniendo sistema...")
        finally:
            self.stop()

    def stop(self):
        self.running = False
        if hasattr(self, 'cap'):
            self.cap.release()
        try:
            cv2.destroyAllWindows()
        except Exception as e:
            print(f"Error cerrando ventanas de OpenCV: {e}")
        self.send_command_to_esp32("STOP")
        print("Sistema detenido")

def main():
    PHONE_IP = "192.168.0.101"
    ESP32_IP = "192.168.1.101"
    waste_detector = WasteDetectionSystem(
        phone_ip=PHONE_IP,
        phone_port=8080,
        esp32_ip=ESP32_IP,
        esp32_port=80
    )
    print("Presiona 'q' para salir")
    waste_detector.run()

if __name__ == "__main__":
    main()