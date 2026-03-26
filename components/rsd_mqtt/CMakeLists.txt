include(${CMAKE_SOURCE_DIR}/rsg_helper.cmake)
get_pio_env_option("custom_rsd_integration" RSD_INTEGRATION)

# задается env в platformio.ini
if(RSD_INTEGRATION STREQUAL "mqtt")

idf_component_register(
    SRCS mqtt_integration.cpp
    INCLUDE_DIRS "include"
    PRIV_REQUIRES rsd_core esp_netif mqtt 
    )
else()
    idf_component_register()
endif()
