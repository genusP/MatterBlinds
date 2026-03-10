set(COMMON_SRCS
    "main.cpp"
    "position_sensor.cpp"
    "motor_control.cpp"
    "calibration.cpp"
    "controller.cpp"
)

#Базовые зависимости
set(COMMON_REQUIRES
    button
)

# Условные зависимости
if(CONFIG_EXTERNAL_INTEGRATION EQUAL 1)
    list(APPEND COMMON_SRCS "matter_intagration.cpp")
    list(APPEND COMMON_REQUIRES rsd_matter)
endif()

if(CONFIG_EXTERNAL_INTEGRATION EQUAL 2)
    list(APPEND COMMON_SRCS "mqtt_integration.cpp")
    list(APPEND COMMON_REQUIRES rsd_mqtt)
endif()

idf_component_register(SRCS ${COMMON_SRCS}
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES ${COMMON_REQUIRES})
