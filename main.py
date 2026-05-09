#!/usr/bin/env python3
"""
Mock сервер осциллографа.

Поддерживает форматы:
- json  : {"timestamp": float, "voltage": float}\n
- csv   : timestamp,voltage\n
- binary: 8 байт: float32 timestamp (little-endian) + float32 voltage (little-endian)

После подключения клиент получает JSON-приветствие с параметрами и выбранным форматом.
"""

import asyncio
import json
import math
import random
import signal
import struct
import time
from argparse import ArgumentParser
from typing import Set


class MockOscilloscopeServer:
    def __init__(
        self,
        host: str = "localhost",
        port: int = 9999,
        sample_rate: int = 100,        # Гц (количество отсчётов в секунду)
        amplitude: float = 5.0,        # Амплитуда сигнала, В
        frequency: float = 10.0,       # Частота сигнала, Гц
        noise_level: float = 0.2,      # Уровень шума, В
        output_format: str = "json",   # "json", "csv" или "binary"
    ):
        self.host = host
        self.port = port
        self.sample_rate = sample_rate
        self.amplitude = amplitude
        self.frequency = frequency
        self.noise_level = noise_level
        self.output_format = output_format.lower()
        if self.output_format not in ("json", "csv", "binary"):
            raise ValueError("format must be 'json', 'csv' or 'binary'")
        self._server = None
        self._clients: Set[asyncio.StreamWriter] = set()
        self._running = True

    async def start(self):
        """Запуск сервера и ожидание подключений."""
        self._server = await asyncio.start_server(
            self._handle_client, self.host, self.port
        )
        print(f"Mock-осциллограф запущен на {self.host}:{self.port}")
        print(f"Параметры: частота дискретизации {self.sample_rate} Гц, "
              f"амплитуда {self.amplitude} В, частота сигнала {self.frequency} Гц, "
              f"формат: {self.output_format}")
        async with self._server:
            await self._server.serve_forever()

    async def _handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        """Обрабатывает одного подключённого клиента."""
        addr = writer.get_extra_info("peername")
        print(f"Новое подключение: {addr}")
        self._clients.add(writer)

        try:
            # 1. Отправляем приветственное JSON-сообщение с параметрами
            greeting = {
                "status": "connected",
                "format": self.output_format,
                "sample_rate_hz": self.sample_rate,
                "amplitude_volts": self.amplitude,
                "frequency_hz": self.frequency,
                "noise_level_volts": self.noise_level,
            }
            writer.write((json.dumps(greeting) + "\n").encode())
            await writer.drain()

            # 2. Подготовка к потоку данных
            start_time = time.monotonic()
            interval = 1.0 / self.sample_rate  # секунд между отсчётами

            # Разные функции отправки в зависимости от формата
            if self.output_format == "binary":
                await self._send_binary_stream(writer, start_time, interval)
            elif self.output_format == "csv":
                await self._send_csv_stream(writer, start_time, interval)
            else:  # json
                await self._send_json_stream(writer, start_time, interval)

        except (ConnectionError, BrokenPipeError, ConnectionResetError, asyncio.CancelledError):
            print(f"Соединение с {addr} потеряно или закрыто клиентом.")
        except Exception as e:
            print(f"Неожиданная ошибка при обработке {addr}: {e}")
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except (BrokenPipeError, ConnectionResetError, RuntimeError):
                pass
            self._clients.discard(writer)
            print(f"Клиент {addr} отключился.")

    async def _send_json_stream(self, writer: asyncio.StreamWriter, start_time: float, interval: float):
        """Отправка данных в формате JSON."""
        while self._running:
            elapsed = time.monotonic() - start_time
            voltage = self._generate_voltage(elapsed)
            data = {
                "timestamp": time.time(),
                "voltage": voltage,
            }
            line = json.dumps(data) + "\n"
            writer.write(line.encode())
            await writer.drain()
            await asyncio.sleep(interval)

    async def _send_csv_stream(self, writer: asyncio.StreamWriter, start_time: float, interval: float):
        """Отправка данных в формате CSV (timestamp,voltage)."""
        while self._running:
            elapsed = time.monotonic() - start_time
            voltage = self._generate_voltage(elapsed)
            line = f"{time.time()},{voltage:.6f}\n"
            writer.write(line.encode())
            await writer.drain()
            await asyncio.sleep(interval)

    async def _send_binary_stream(self, writer: asyncio.StreamWriter, start_time: float, interval: float):
        """Отправка данных в бинарном формате: 8 байт (float32 timestamp, float32 voltage), little-endian."""
        while self._running:
            elapsed = time.monotonic() - start_time
            voltage = self._generate_voltage(elapsed)
            timestamp = time.time()
            # упаковка в little-endian float32 (4 байта каждое)
            packed = struct.pack('<ff', timestamp, voltage)
            writer.write(packed)
            await writer.drain()
            await asyncio.sleep(interval)

    def _generate_voltage(self, elapsed: float) -> float:
        """Генерация значения напряжения: синусоида + случайный шум."""
        signal = self.amplitude * math.sin(2 * math.pi * self.frequency * elapsed)
        noise = random.uniform(-self.noise_level, self.noise_level)
        return signal + noise

    async def stop(self):
        """Остановка сервера и закрытие всех соединений."""
        self._running = False
        for writer in self._clients:
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass
        if self._server:
            self._server.close()
            await self._server.wait_closed()


async def main():
    parser = ArgumentParser(description="Mock-сервер осциллографа (TCP)")
    parser.add_argument("--host", default="localhost", help="Адрес для прослушивания")
    parser.add_argument("--port", type=int, default=9999, help="TCP-порт")
    parser.add_argument("--sample-rate", type=int, default=100, help="Частота дискретизации (Гц)")
    parser.add_argument("--amplitude", type=float, default=5.0, help="Амплитуда сигнала (В)")
    parser.add_argument("--frequency", type=float, default=10.0, help="Частота сигнала (Гц)")
    parser.add_argument("--noise", type=float, default=0.2, help="Уровень шума (В)")
    parser.add_argument(
        "--format", choices=["json", "csv", "binary"], default="json",
        help="Формат выходных данных"
    )
    args = parser.parse_args()

    server = MockOscilloscopeServer(
        host=args.host,
        port=args.port,
        sample_rate=args.sample_rate,
        amplitude=args.amplitude,
        frequency=args.frequency,
        noise_level=args.noise,
        output_format=args.format,
    )

    loop = asyncio.get_running_loop()
    stop_event = asyncio.Event()

    def _signal_handler():
        print("\nПолучен сигнал остановки. Завершение работы...")
        stop_event.set()

    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, _signal_handler)

    server_task = asyncio.create_task(server.start())
    await stop_event.wait()
    await server.stop()
    server_task.cancel()
    print("Сервер остановлен.")


if __name__ == "__main__":
    asyncio.run(main())

