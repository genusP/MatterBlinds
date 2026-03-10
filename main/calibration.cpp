// main/calibration.cpp
#include "calibration.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "calibration";

static calibration_data_t calib_data = {};

calibration_data_t calibration_start()
{
    calib_data = {
        .state = PREPARE};

    ESP_LOGI(TAG, "Снимите цепочку и нажмите любую кнопку");
    return calib_data;
}

calibration_data_t calibration_next()
{
    switch (calib_data.state)
    {
    case PREPARE: // пользовательподтвердил удаление цепочки
        // выход в верхнюю позицию
        motor_move_to_position(100);
        calib_data.state = SETUP_TOP;
        ESP_LOGI(TAG, "Смотайте штору, наденьте цепочку и нажмите любую кнопку");
        break;
    case SETUP_TOP:
        calib_data.top_limit = position_sensor_read();
        calib_data.state = SETUP_BOTTOM;
        ESP_LOGI(TAG, "Переведите штору в кранее нижнее положение и нажмите кноку");
        break;
    case SETUP_BOTTOM:
        calib_data.bottom_limit = position_sensor_read();
#ifdef CONFIG_ZEBRA_BLINDS_SUPPORT
        calib_data.state = SETUP_ZEBRA;
        ESP_LOGI(TAG, "Поднимите штору до ближайшего пересечения прозрачных участков и нажмите кноку");
        break;
    case SETUP_ZEBRA:
        calib_data.zebra_offset = calib_data.bottom_limit - position_sensor_read();
#endif
        calib_data.state = CALIBRATED;
        ESP_LOGI(TAG, "Настройка завершена.");
        break;
    default:
        break;
    }
    return calib_data;
}

void calibration_cancel()
{
    calib_data = {
        .state = NONE};
}

void save_calibration_data(calibration_data_t calib)
{
    if (calib.state != CALIBRATED)
    {
        ESP_LOGE(TAG, "Нельзя сохранять незавершенную калибровку");
        return;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(TAG, NVS_READWRITE, &nvs_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_u16(nvs_handle, "top", calib.top_limit);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Error saving top position: %s", esp_err_to_name(err));

    err = nvs_set_u16(nvs_handle, "bottom", calib.bottom_limit);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Error saving bottom position: %s", esp_err_to_name(err));
#ifdef CONFIG_ZEBRA_BLINDS_SUPPORT
    err = nvs_set_u16(nvs_handle, "zebra_offset", calib.zebra_offset);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Error saving zebra offset: %s", esp_err_to_name(err));
#endif

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Calibration data saved to NVS");
}

bool read_calibration_data(calibration_data_t *calib)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(TAG, NVS_READWRITE, &nvs_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_get_u16(nvs_handle, "top", &calib->top_limit);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error reading top position: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_get_u16(nvs_handle, "bottom", &calib->bottom_limit);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error reading bottom position: %s", esp_err_to_name(err));
        return false;
    }
#ifdef CONFIG_ZEBRA_BLINDS_SUPPORT
    err = nvs_get_u16(nvs_handle, "zebra_offset", &calib->zebra_offset);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error reading zebra offset: %s", esp_err_to_name(err));
        return false;
    }
#endif

    nvs_close(nvs_handle);

    calib->state = CALIBRATED;
    ESP_LOGI(TAG, "Calibration data readed from NVS");
    return true;
}
