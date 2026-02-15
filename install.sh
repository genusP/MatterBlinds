#!/bin/bash
set -e

# 1. Активируем IDF
. $IDF_PATH/export.sh

# 2. Клонируем оболочку Matter (без сабмодулей пока что)
if [ ! -d "esp-matter" ]; then
    git clone --depth 1 https://github.com/espressif/esp-matter.git
fi

cd esp-matter

# 3. Инициализируем только базовые сабмодули самого Matter
git submodule update --init --depth 1

# 4. Входим в недра ConnectedHomeIP и качаем только для ESP32 и Linux
cd ./connectedhomeip/connectedhomeip
./scripts/checkout_submodules.py --platform esp32 linux --shallow

# 5. Возвращаемся и запускаем финальную установку инструментов
cd ../..
./install.sh

cd ..
echo "Установка по официальному методу завершена!"
