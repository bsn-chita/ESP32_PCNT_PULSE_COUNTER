#include <stdio.h>
#include "driver/pulse_cnt.h" // Новый драйвер счетчика импульсов (PCNT)
#include "driver/gpio.h"      // Драйвер управления пинами
#include "esp_log.h"          // Библиотека для вывода логов в консоль
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pcnt.h"

static const char *TAG = "ELS_PROJECT";

void app_main(void) {

    ESP_LOGI(TAG, "Запуск электронной гитары...");

    // Инициализация энкодера (тут вылетят все ESP_LOGI из вашего модуля)
    pcnt_unit_handle_t encoder = init_encoder();

    ESP_LOGI(TAG, "Энкодер успешно инициализирован.");

    // Бесконечный цикл, чтобы программа не завершалась
    while(1) {
        int64_t pos = get_encoder_total_pulses(encoder);
        ESP_LOGI(TAG, "Текущая позиция шпинделя: %lld", pos);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Выводим позицию раз в секунду
    }

}
