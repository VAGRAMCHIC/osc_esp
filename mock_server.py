#!/usr/bin/env python3
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
        sample_rate: int = 100,
        signal_type: str = "sine",
        amplitude: float = 5.0,
        frequency: float = 10.0,
        offset: float = 0.0,
        noise_level: float = 0.0,
        output_format: str = "json",
        led_on_voltage: float = 3.6,
        led_off_voltage: float = 0.0,
        led_duty_cycle: float = 0.5,
    ):
        self.host = host
        self.port = port
        self.sample_rate = sample_rate
        self.signal_type = signal_type.lower()
        self.amplitude = amplitude
        self.frequency = frequency
        self.offset = offset
        self.noise_level = noise_level
        self.output_format = output_format.lower()
        # Параметры для LED
        self.led_on_voltage = led_on_voltage
        self.led_off_voltage = led_off_voltage
        self.led_duty_cycle = max(0.0, min(1.0, led_duty_cycle))  # ограничиваем [0,1]

        if self.output_format not in ("json", "csv", "binary"):
            raise ValueError("format must be 'json', 'csv' or 'binary'")
        allowed_signals = ("sine", "square", "triangle", "sawtooth", "noise", "led")
        if self.signal_type not in allowed_signals:
            raise ValueError(f"signal must be one of: {', '.join(allowed_signals)}")
        if self.signal_type == "led" and self.frequency <= 0:
            # Если частота не задана или равна нулю, генерируем постоянный уровень включения
            self.frequency = 0.0

        self._server = None
        self._clients = set()
        self._running = True

    async def start(self):
        self._server = await asyncio.start_server(
            self._handle_client, self.host, self.port
        )
        print(f"Mock-осциллограф запущен на {self.host}:{self.port}")
        base_info = (f"Параметры: дискретизация {self.sample_rate} Гц, "
                     f"сигнал {self.signal_type}")
        if self.signal_type == "led":
            print(f"{base_info}, частота {self.frequency} Гц, "
                  f"вкл {self.led_on_voltage} В, выкл {self.led_off_voltage} В, "
                  f"скважность {self.led_duty_cycle:.2f}, "
                  f"смещение {self.offset} В, шум {self.noise_level} В, "
                  f"формат {self.output_format}")
        else:
            print(f"{base_info}, амплитуда {self.amplitude} В, "
                  f"частота {self.frequency} Гц, смещение {self.offset} В, "
                  f"шум {self.noise_level} В, формат {self.output_format}")
        async with self._server:
            await self._server.serve_forever()

    async def _handle_client(self, reader, writer):
        addr = writer.get_extra_info("peername")
        print(f"Новое подключение: {addr}")
        self._clients.add(writer)

        try:
            greeting = {
                "status": "connected",
                "format": self.output_format,
                "signal_type": self.signal_type,
                "sample_rate_hz": self.sample_rate,
                "frequency_hz": self.frequency,
                "offset_volts": self.offset,
                "noise_level_volts": self.noise_level,
            }
            if self.signal_type == "led":
                greeting.update({
                    "led_on_voltage": self.led_on_voltage,
                    "led_off_voltage": self.led_off_voltage,
                    "led_duty_cycle": self.led_duty_cycle,
                })
            else:
                greeting["amplitude_volts"] = self.amplitude

            writer.write((json.dumps(greeting) + "\n").encode())
            await writer.drain()

            start_time = time.monotonic()
            interval = 1.0 / self.sample_rate

            if self.output_format == "binary":
                await self._send_binary_stream(writer, start_time, interval)
            elif self.output_format == "csv":
                await self._send_csv_stream(writer, start_time, interval)
            else:
                await self._send_json_stream(writer, start_time, interval)

        except (ConnectionError, BrokenPipeError, ConnectionResetError, asyncio.CancelledError):
            print(f"Соединение с {addr} потеряно")
        except Exception as e:
            print(f"Ошибка: {e}")
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass
            self._clients.discard(writer)
            print(f"Клиент {addr} отключился")

    def _generate_voltage(self, t: float) -> float:
        """Генерирует значение в зависимости от типа сигнала."""
        # Для LED используется своя логика
        if self.signal_type == "led":
            if self.frequency <= 0:
                value = self.led_on_voltage
            else:
                period = 1.0 / self.frequency
                phase = (t % period) / period  # от 0 до 1
                if phase < self.led_duty_cycle:
                    value = self.led_on_voltage
                else:
                    value = self.led_off_voltage
        else:
            phase = 2 * math.pi * self.frequency * t
            if self.signal_type == "sine":
                value = self.amplitude * math.sin(phase)
            elif self.signal_type == "square":
                value = self.amplitude * (1.0 if math.sin(phase) >= 0 else -1.0)
            elif self.signal_type == "triangle":
                saw = 2.0 * (phase / (2*math.pi) - math.floor(phase / (2*math.pi) + 0.5))
                value = self.amplitude * (2.0 * abs(saw) - 1.0)
            elif self.signal_type == "sawtooth":
                saw = 2.0 * (phase / (2*math.pi) - math.floor(phase / (2*math.pi) + 0.5))
                value = self.amplitude * saw
            elif self.signal_type == "noise":
                value = self.amplitude * (2.0 * random.random() - 1.0)
            else:
                value = 0.0

        # Добавляем смещение и шум (для всех типов сигналов)
        noise = random.gauss(0, self.noise_level)
        return self.offset + value + noise

    async def _send_json_stream(self, writer, start_time, interval):
        while self._running:
            elapsed = time.monotonic() - start_time
            voltage = self._generate_voltage(elapsed)
            data = {"timestamp": time.time(), "voltage": voltage}
            line = json.dumps(data) + "\n"
            writer.write(line.encode())
            await writer.drain()
            await asyncio.sleep(interval)

    async def _send_csv_stream(self, writer, start_time, interval):
        while self._running:
            elapsed = time.monotonic() - start_time
            voltage = self._generate_voltage(elapsed)
            line = f"{time.time()},{voltage:.6f}\n"
            writer.write(line.encode())
            await writer.drain()
            await asyncio.sleep(interval)

    async def _send_binary_stream(self, writer, start_time, interval):
        while self._running:
            elapsed = time.monotonic() - start_time
            voltage = self._generate_voltage(elapsed)
            timestamp = time.time()
            packed = struct.pack('<ff', timestamp, voltage)
            writer.write(packed)
            await writer.drain()
            await asyncio.sleep(interval)

    async def stop(self):
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
    parser = ArgumentParser(description="Mock-сервер осциллографа с различными сигналами")
    parser.add_argument("--host", default="localhost", help="Адрес для прослушивания")
    parser.add_argument("--port", type=int, default=9999, help="TCP-порт")
    parser.add_argument("--sample-rate", type=int, default=100, help="Частота дискретизации (Гц)")
    parser.add_argument("--signal", choices=["sine", "square", "triangle", "sawtooth", "noise", "led"],
                        default="sine", help="Тип сигнала")
    parser.add_argument("--amplitude", type=float, default=5.0, help="Амплитуда сигнала (В) (не используется для led)")
    parser.add_argument("--frequency", type=float, default=10.0, help="Частота сигнала (Гц)")
    parser.add_argument("--offset", type=float, default=0.0, help="Постоянное смещение (В)")
    parser.add_argument("--noise", type=float, default=0.0, help="Уровень шума (В)")
    parser.add_argument("--format", choices=["json", "csv", "binary"], default="json",
                        help="Формат выходных данных")
    # Параметры для LED
    parser.add_argument("--led-on-voltage", type=float, default=3.6,
                        help="Напряжение включения светодиода (В) (только для сигнала led)")
    parser.add_argument("--led-off-voltage", type=float, default=0.0,
                        help="Напряжение выключения светодиода (В) (только для сигнала led)")
    parser.add_argument("--led-duty-cycle", type=float, default=0.5,
                        help="Скважность (доля периода, когда светодиод включён) (0..1, только для led)")
    args = parser.parse_args()

    server = MockOscilloscopeServer(
        host=args.host,
        port=args.port,
        sample_rate=args.sample_rate,
        signal_type=args.signal,
        amplitude=args.amplitude,
        frequency=args.frequency,
        offset=args.offset,
        noise_level=args.noise,
        output_format=args.format,
        led_on_voltage=args.led_on_voltage,
        led_off_voltage=args.led_off_voltage,
        led_duty_cycle=args.led_duty_cycle,
    )

    loop = asyncio.get_running_loop()
    stop_event = asyncio.Event()

    def _signal_handler():
        print("\nОстановка сервера...")
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
