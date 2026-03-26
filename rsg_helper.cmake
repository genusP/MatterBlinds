# макрос читающий настройку из текущей конфигурации 
# требуется для корректной работы при построении зависимостей компонентов
macro(get_pio_env_option OPTION_NAME RESULT_VAR)
    # 1. Пытаемся достать из пути (второй проход)
    if(CMAKE_BINARY_DIR MATCHES ".pio/build/([^/]*)")
        set(CURRENT_PIO_ENV "${CMAKE_MATCH_1}")
    endif()

    # 2. Если не нашли (первый проход), перебираем аргументы вызова CMake
    if(NOT CURRENT_PIO_ENV)
        # CMAKE_ARGV — это список всех аргументов командной строки
        math(EXPR _ARG_END "${CMAKE_ARGC} - 1")
        foreach(_I RANGE ${_ARG_END})
            set(_ARG "${CMAKE_ARGV${_I}}")
            # PIO часто передает пути, содержащие имя окружения в .pio/build/NAME
            if(_ARG MATCHES ".pio/build/([^/ ]+)")
                set(CURRENT_PIO_ENV "${CMAKE_MATCH_1}")
                break()
            endif()
        endforeach()
    endif()

    # file(APPEND "${CMAKE_SOURCE_DIR}/build_log.txt" "PIO env at ${CURRENT_PIO_ENV}\n")
    if(EXISTS "${CMAKE_SOURCE_DIR}/platformio.ini" AND CURRENT_PIO_ENV)
        # file(APPEND "${CMAKE_SOURCE_DIR}/build_log.txt" "read platformio\n")
        file(READ "${CMAKE_SOURCE_DIR}/platformio.ini" PIO_CONFIG)
        
        # Ищем секцию [env:NAME] до следующей секции [
        string(REGEX MATCH "\\[env:${CURRENT_PIO_ENV}\\][^[]+" ENV_SECTION "${PIO_CONFIG}")
        
        # file(APPEND "${CMAKE_SOURCE_DIR}/build_log.txt" "env section at ${ENV_SECTION}\n")
        if(ENV_SECTION)
            # Ищем ключ = значение
            if(ENV_SECTION MATCHES "${OPTION_NAME} *= *\"?([^\n\r\t\"]+)\"?")
                # file(APPEND "${CMAKE_SOURCE_DIR}/build_log.txt" "Value at ${CMAKE_MATCH_1}\n")
                set(${RESULT_VAR} "${CMAKE_MATCH_1}")
            endif()
        endif()
    endif()
endmacro()
