#!/usr/bin/env python3
import socket
import json
import struct
import time

def main():
    host = "192.168.88.234"  # IP вашего ESP32
    port = 9999

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)    # таймаут на случай проблем
    sock.connect((host, port))

    # Приветствие
    greeting_line = sock.recv(1024).decode().strip()
    print(f"[Приветствие] {greeting_line}")
    greeting = json.loads(greeting_line)
    data_format = greeting.get("format", "binary")
    print(f"Формат: {data_format}")
    
    if data_format == "binary":
        print("Чтение 10 бинарных пакетов (8 байт каждый)...")
        packets_read = 0
        buffer = b''  # буфер для неполных данных
        while packets_read < 10:
            try:
                chunk = sock.recv(1024)   # читаем блок
                if not chunk:
                    break
                buffer += chunk
                # Извлекаем полные пакеты
                while len(buffer) >= 8:
                    packet = buffer[:8]
                    buffer = buffer[8:]
                    timestamp, voltage = struct.unpack('<ff', packet)
                    print(f"{packets_read+1:2d}: t={timestamp:.3f}  V={voltage:.3f} В")
                    packets_read += 1
                    if packets_read >= 10:
                        break
            except socket.timeout:
                print("Таймаут, данных больше нет")
                break
    else:
        # Аналогично для текстовых форматов...
        pass

    sock.close()
    print("Соединение закрыто.")

if __name__ == "__main__":
    main()
