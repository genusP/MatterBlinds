#!/bin/bash
# Клонируем ESP-Matter
git clone --depth 1 --recursive https://github.com/espressif/esp-matter.git
cd esp-matter
./install.sh
cd ..
# Настройка переменных окружения
. $IDF_PATH/export.sh
