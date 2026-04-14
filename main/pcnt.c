#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "PCNT";

#define ENCODER_A_PIN CONFIG_PCNT_PIN_A
#define ENCODER_B_PIN CONFIG_PCNT_PIN_B
#define ENCODER_Z_PIN CONFIG_PCNT_PIN_Z

// << Параметры железных ограничений PCNT (счетчик 16-битный)
#define PCNT_HIGH_LIMIT 32767
#define PCNT_LOW_LIMIT  -32768
// Параметры железных ограничений PCNT (счетчик 16-битный) >>

// Глобальные переменные для хранения "бесконечного" счета
volatile int64_t total_pulses = 0;       // Общее кол-во импульсов за всё время
volatile int64_t z_pos_absolute = 0;     // Позиция ABS в момент срабатывания Z

// Для чтения int64_t в программе (не в прерывании)
int64_t get_encoder_total_pulses(pcnt_unit_handle_t unit) {
    int cur_hw_val = 0;
    int64_t total;

    // В ESP32 самый простой способ безопасно прочитать 64-битное число,
    // которое меняется в прерывании — прочитать его дважды и сравнить.
    do {
        total = total_pulses;
        pcnt_unit_get_count(unit, &cur_hw_val);
    } while (total != total_pulses); // Если за время чтения случилось прерывание — повторить

    return total + cur_hw_val;
}

//Обработчик прерывания ПЕРЕПОЛНЕНИЯ счетчика.
//Вызывается аппаратно, когда счетчик доходит до 32767 или -32768.
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

//Обработчик прерывания Z-метки.
//Срабатывает один раз за оборот.
static void IRAM_ATTR encoder_z_isr_handler(void *arg) {
    int current_hw_val = 0;
    pcnt_unit_get_count((pcnt_unit_handle_t)arg, &current_hw_val);

    // Запоминаем точную абсолютную позицию в момент метки
    // z_pos_absolute = (int64_t)(total_pulses + current_hw_val);
    z_pos_absolute = total_pulses + current_hw_val;
}

// << Инициализация PCNT
pcnt_unit_handle_t init_encoder() {
    ESP_LOGI(TAG, "Настройка энкодера (PCNT):");
    // 1. Конфигурация юнита
    ESP_LOGI(TAG, "Конфигурирование unit (PCNT):");
    ESP_LOGI(TAG, " - PCNT HIGH LIMIT %d", PCNT_HIGH_LIMIT);
    ESP_LOGI(TAG, " - PCNT LOW LIMIT %d", PCNT_LOW_LIMIT);
    pcnt_unit_config_t unit_config = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit = PCNT_LOW_LIMIT,
    };
    pcnt_unit_handle_t pcnt_unit = NULL;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));
    // 1. Конфигурация юнита

    // 2. Фильтр помех (игнорирует всплески короче CONFIG_PCNT_GLITCH_FILTER_NS)
    ESP_LOGI(TAG, " - Фильтр помех: %d нс", CONFIG_PCNT_GLITCH_FILTER_NS);
    pcnt_glitch_filter_config_t filter_config = { .max_glitch_ns = CONFIG_PCNT_GLITCH_FILTER_NS };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));
    // 2. Фильтр помех (игнорирует всплески короче CONFIG_PCNT_GLITCH_FILTER_NS)

    // 3. Настройка Каналов (A и B).

    // Канал А:
    ESP_LOGI(TAG, " - Фаза A: GPIO %d", CONFIG_PCNT_PIN_A);
    pcnt_chan_config_t chan_a_config = { .edge_gpio_num = ENCODER_A_PIN, .level_gpio_num = ENCODER_B_PIN };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));
    // Канал А

    // Канал B:
    ESP_LOGI(TAG, " - Фаза B: GPIO %d", CONFIG_PCNT_PIN_B);
    pcnt_chan_config_t chan_b_config = { .edge_gpio_num = ENCODER_B_PIN, .level_gpio_num = ENCODER_A_PIN };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));
    // Канал B

    // 3. Настройка Каналов (A и B).

    // 4. Логика квадратурного счета (x4)

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_INVERSE, PCNT_CHANNEL_LEVEL_ACTION_KEEP));

    // 4. Логика квадратурного счета (x4)

    // 5. Настройка событий (Watch Points)
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, PCNT_HIGH_LIMIT));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, PCNT_LOW_LIMIT));

    pcnt_event_callbacks_t cbs = { .on_reach = on_pcnt_reach_limit };
    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit, &cbs, NULL));
    // 5. Настройка событий (Watch Points)

    // 6. Включение железа (обязательно ДО старта)
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    // 6. Включение железа (обязательно ДО старта)

    // 7. Настройка пина Z (через обычное GPIO прерывание)
    ESP_LOGI(TAG, " - Индекс Z: GPIO %d", CONFIG_PCNT_PIN_Z);

    // Настраиваем пин для Z (иначе не срабатывает обработчик)
    gpio_config_t z_conf = {
        .pin_bit_mask = (1ULL << ENCODER_Z_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // Включаем внутреннюю подтяжку для теста
        .intr_type = GPIO_INTR_NEGEDGE,    // Сработка по спаду
    };
    gpio_config(&z_conf);
    // Настраиваем пин для Z (иначе не срабатывает обработчик)

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err); // Пропускаем ошибку "уже установлен"
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(ENCODER_Z_PIN, encoder_z_isr_handler, pcnt_unit));
    ESP_ERROR_CHECK(gpio_set_intr_type(ENCODER_Z_PIN, GPIO_INTR_NEGEDGE));

    // 7. Настройка пина Z (через обычное GPIO прерывание)

    // 8. Поехали!
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
    return pcnt_unit;
}
// Инициализация PCNT >>
