set(COMMON_SRCS
    "main.c"
)


# Условные зависимости
if (CONFIG_EXTERNAL_INTEGRATION_MATTER)
    list(APPEND COMMON_SRCS "main.cpp" "wifi.cpp" "matter_integration.cpp")
    list(APPEND COMMON_REQUIRES rsd_matter)
endif()

if (CONFIG_EXTERNAL_INTEGRATION_MQTT)
    list(APPEND COMMON_SRCS "wifi.cpp" "mqtt_integration.cpp")
    list(APPEND COMMON_REQUIRES rsd_mqtt)
endif()

idf_component_register(SRCS ${COMMON_SRCS}
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES ${COMMON_REQUIRES})
