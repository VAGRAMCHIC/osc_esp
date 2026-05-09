#!/usr/bin/env python3
"""
Консольный клиент для Mock-сервера осциллографа.
Поддерживает форматы: json, csv, binary (float32 little-endian).
"""

import socket
import json
import struct


def main():
    host = "192.168.88.237"
    port = 9999

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))

    # Читаем приветственное сообщение (JSON-строка, заканчивается \n)
    greeting_line = sock.recv(1024).decode().strip()
    print(f"[Приветствие] {greeting_line}")

    try:
        greeting = json.loads(greeting_line)
    except json.JSONDecodeError:
        print("Ошибка: не удалось разобрать приветствие")
        sock.close()
        return

    data_format = greeting.get("format", "json")
    print(f"Формат данных: {data_format}")

    if data_format == "binary":
        # Бинарный режим: каждый отсчёт – 8 байт (timestamp float32, voltage float32)
        print("Чтение 10 бинарных отсчётов...")
        for i in range(10):
            # Читаем ровно 8 байт (один пакет)
            packet = sock.recv(8)
            if len(packet) < 8:
                print(f"Недостаточно данных: получено {len(packet)} байт, ожидалось 8")
                break
            timestamp, voltage = struct.unpack('<ff', packet)
            print(f"{i+1:2d}: t={timestamp:.3f}  V={voltage:.3f} В")
    else:
        # Текстовые форматы (json или csv): каждая строка заканчивается \n
        print("Чтение 10 текстовых строк...")
        for i in range(10):
            line = sock.recv(1024).decode().strip()
            if not line:
                break
            if data_format == "json":
                try:
                    data = json.loads(line)
                    print(f"{i+1:2d}: timestamp={data.get('timestamp', 0):.3f}  voltage={data.get('voltage', 0):.3f} В")
                except:
                    print(f"{i+1:2d}: {line} (не JSON)")
            else:  # csv
                parts = line.split(',')
                if len(parts) >= 2:
                    timestamp = float(parts[0])
                    voltage = float(parts[1])
                    print(f"{i+1:2d}: timestamp={timestamp:.3f}  voltage={voltage:.3f} В")
                else:
                    print(f"{i+1:2d}: {line} (не CSV)")

    sock.close()
    print("Соединение закрыто.")


if __name__ == "__main__":
    main()

