FROM ubuntu:22.04

# Ставим зависимости для PIO и сборки Matter/Zigbee
RUN apt-get update && apt-get install -y \
    python3 python3-pip python3-venv \
    git curl cmake ninja-build gperf \
    libusb-1.0-0 libudev-dev \
    && apt-get clean

# Устанавливаем PlatformIO Core
RUN pip3 install --no-cache-dir -U platformio

RUN pio pkg install --global \
    --platform https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip 

# Создаем рабочую директорию
WORKDIR /workspace

# Отключаем телеметрию PIO, чтобы не стучалась в заблокированные домены
ENV PLATFORMIO_DISABLE_TELEMETRY=1
