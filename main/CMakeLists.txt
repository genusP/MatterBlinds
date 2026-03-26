set(RSD_SRC "main.cpp")

if(RSD_USE_WIFI)
    list(APPEND RSD_SRC "wifi.cpp")
endif()

idf_component_register(
    SRCS ${RSD_SRC}
    INCLUDE_DIRS "."
    REQUIRES rsd_core rsd_mqtt rsd_matter
)
