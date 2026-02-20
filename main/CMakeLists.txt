# Базовые исходные файлы
set(COMMON_SRCS
    "main.cpp"
    "position_sensor.cpp"
    "button_handler.cpp"
    "motor_control.cpp"
    "controller.cpp"
)

# Условная компиляция для Matter
if(CONFIG_ENABLE_MATTER_INTEGRATION)
    list(APPEND COMMON_SRCS "matter_integration.cpp")
endif()

# Условная компиляция для MQTT
if(CONFIG_ENABLE_MQTT_INTEGRATION)
    list(APPEND COMMON_SRCS "mqtt_integration.cpp")
endif()

# Базовые зависимости
set(COMMON_REQUIRES
    button
)

# Условные зависимости
if(CONFIG_ENABLE_MATTER_INTEGRATION)
    list(APPEND COMMON_REQUIRES esp_matter)
endif()

if(CONFIG_ENABLE_MQTT_INTEGRATION)
    list(APPEND COMMON_REQUIRES esp_mqtt mqtt)
endif()

idf_component_register(SRCS ${COMMON_SRCS}
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES ${COMMON_REQUIRES})
