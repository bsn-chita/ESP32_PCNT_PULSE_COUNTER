#include <stdio.h>
#include "driver/pulse_cnt.h" // Новый драйвер счетчика импульсов (PCNT)
#include "driver/gpio.h"      // Драйвер управления пинами
#include "esp_log.h"          // Библиотека для вывода логов в консоль
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ELS_PROJECT";

// Настройки пинов (34, 35, 36 - только вход, без внутренних подтяжек)
#define ENCODER_A_PIN 34
#define ENCODER_B_PIN 35
#define ENCODER_Z_PIN 25

// Параметры железных ограничений PCNT (счетчик 16-битный)
#define PCNT_HIGH_LIMIT 32767
#define PCNT_LOW_LIMIT  -32768

// Глобальные переменные для хранения "бесконечного" счета
volatile int64_t total_pulses = 0;       // Общее кол-во импульсов за всё время
volatile int32_t z_marks_detected = 0;   // Сколько раз видели метку Z
volatile int32_t z_pos_absolute = 0;     // Позиция ABS в момент срабатывания Z

/**
 * Обработчик прерывания ПЕРЕПОЛНЕНИЯ счетчика.
 * Вызывается аппаратно, когда счетчик доходит до 32767 или -32768.
 */
static bool IRAM_ATTR on_pcnt_reach_limit(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx) {
    // Если дошли до верха, добавляем 32767 к общему счету
    if (edata->watch_point_value == PCNT_HIGH_LIMIT) {
        total_pulses += PCNT_HIGH_LIMIT;
    }
    // Если дошли до низа, вычитаем
    else if (edata->watch_point_value == PCNT_LOW_LIMIT) {
        total_pulses += PCNT_LOW_LIMIT;
    }
    return false;
}

/**
 * Обработчик прерывания Z-метки (пин 36).
 * Срабатывает один раз за оборот.
 */
static void IRAM_ATTR encoder_z_isr_handler(void *arg) {
    int current_hw_val = 0;
    pcnt_unit_get_count((pcnt_unit_handle_t)arg, &current_hw_val);

    // Запоминаем точную абсолютную позицию в момент метки
    z_pos_absolute = (int32_t)(total_pulses + current_hw_val);
    z_marks_detected++;

}

pcnt_unit_handle_t init_encoder() {
    ESP_LOGI(TAG, "Настройка входов и PCNT...");

    // 1. Конфигурация юнита (мозги счетчика)
    pcnt_unit_config_t unit_config = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit = PCNT_LOW_LIMIT,
    };
    pcnt_unit_handle_t pcnt_unit = NULL;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    // 2. Фильтр помех (игнорирует всплески короче 12.5 мкс)
    pcnt_glitch_filter_config_t filter_config = { .max_glitch_ns = 12500 };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    // 3. Настройка Каналов (A и B)
    // Канал А: следит за пином А, используя пин B как контрольный для направления
    pcnt_chan_config_t chan_a_config = { .edge_gpio_num = ENCODER_A_PIN, .level_gpio_num = ENCODER_B_PIN };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    // Канал B: наоборот (создает режим x4)
    pcnt_chan_config_t chan_b_config = { .edge_gpio_num = ENCODER_B_PIN, .level_gpio_num = ENCODER_A_PIN };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));

    // 4. Логика квадратурного счета (x4)
    // Настраиваем так, чтобы при вращении в одну сторону оба канала прибавляли
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_INVERSE, PCNT_CHANNEL_LEVEL_ACTION_KEEP));

    // 5. Настройка событий (Watch Points) - чтобы CPU знал, когда счетчик переполнился
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, PCNT_HIGH_LIMIT));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, PCNT_LOW_LIMIT));

    pcnt_event_callbacks_t cbs = { .on_reach = on_pcnt_reach_limit };
    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit, &cbs, NULL));

    // 6. Включение железа (обязательно ДО старта)
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));

    // 7. Настройка пина Z (через обычное GPIO прерывание)
    gpio_install_isr_service(0);
    ESP_ERROR_CHECK(gpio_isr_handler_add(ENCODER_Z_PIN, encoder_z_isr_handler, pcnt_unit));
    ESP_ERROR_CHECK(gpio_set_intr_type(ENCODER_Z_PIN, GPIO_INTR_NEGEDGE));

    // 8. Поехали!
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
    return pcnt_unit;
}

void app_main(void) {


    gpio_config_t z_conf = {
    .pin_bit_mask = (1ULL << ENCODER_Z_PIN),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE, // Включаем внутреннюю подтяжку для теста
    .intr_type = GPIO_INTR_NEGEDGE,    // Сработка по спаду
};
gpio_config(&z_conf);

// Установка сервиса с флагом ESP_INTR_FLAG_IRAM
// Это критично, так как обработчик в IRAM
esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
if (err == ESP_ERR_INVALID_STATE) {
    // Сервис уже запущен, это нормально
}


    pcnt_unit_handle_t encoder = init_encoder();
    int current_hw_val = 0;

    ESP_LOGI(TAG, "F2");

    while (1) {
        // Получаем текущее значение из железного регистра
        pcnt_unit_get_count(encoder, &current_hw_val);

        // Склеиваем его с количеством переполнений
        int64_t full_abs_pos = total_pulses + current_hw_val;

        // Вывод: ABS позиция, кол-во кругов и отклонение Z от кратного 7200
        printf("ABS: %lld | Z_Count: %ld | Z_Modulo: %ld\n",
                full_abs_pos, z_marks_detected, z_pos_absolute % 7200);

        vTaskDelay(pdMS_TO_TICKS(100)); // Опрос 10 раз в секунду для консоли
    }
}
