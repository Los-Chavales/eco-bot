import socket

ip_esp32 = "192.168.1.100"  # Cambia por IP del ESP32
puerto = 80

s = socket.socket()
s.connect((ip_esp32, puerto))
s.send(b"AVANZAR\n")
s.close()
