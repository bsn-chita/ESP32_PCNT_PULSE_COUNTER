#include <stdio.h>
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#define ENCODER_A_PIN 34
#define ENCODER_B_PIN 35
#define ENCODER_Z_PIN 36

static const char *TAG = "PCNT";

#define ENCODER_LINES 1800

#define PCNT_HIGH_LIMIT 32767
#define PCNT_LOW_LIMIT  -32768






/*
volatile int last_reset_at = 0; // Переменная для хранения "предсмертного" значения

// 2. Настройка прерывания на сброс (Software-based Clear)
// В PCNT 5.5 нет прямой функции "pin 36 == reset",
// поэтому мы создаем Watchpoint или используем GPIO ISR.
// Для начала сделаем через GPIO прерывание для проверки:

static void IRAM_ATTR encoder_z_isr_handler(void *arg) {
    //pcnt_unit_handle_t unit = (pcnt_unit_handle_t)arg;
    //pcnt_unit_clear_count(unit);
    pcnt_unit_handle_t unit = (pcnt_unit_handle_t)arg;
    pcnt_unit_get_count(unit, (int*)&last_reset_at); // Запоминаем, сколько было
    pcnt_unit_clear_count(unit);
}
*/

// Глобальные переменные для теста
volatile int64_t total_pulses = 0;
volatile int32_t z_count_at_last_mark = 0;
volatile int32_t z_marks_detected = 0;


// 1. Убедитесь, что обработчик соответствует сигнатуре pcnt_watch_cb_t
static bool IRAM_ATTR on_pcnt_reach_limit(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx) {
    if (edata->watch_point_value == PCNT_HIGH_LIMIT) {
        total_pulses += PCNT_HIGH_LIMIT;
    } else if (edata->watch_point_value == PCNT_LOW_LIMIT) {
        total_pulses += PCNT_LOW_LIMIT;
    }
    return false;
}

/*
// Обработчик переполнения (когда PCNT доходит до +/- 32768)
static bool IRAM_ATTR on_pcnt_reach_limit(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx) {
    if (edata->watch_point_value == PCNT_HIGH_LIMIT) total_pulses += PCNT_HIGH_LIMIT;
    else if (edata->watch_point_value == PCNT_LOW_LIMIT) total_pulses += PCNT_LOW_LIMIT;
    return false;
}
*/

// Обработчик Z-метки (без сброса!)
static void IRAM_ATTR encoder_z_isr_handler(void *arg) {
    /*
    pcnt_unit_handle_t unit = (pcnt_unit_handle_t)arg;
    int current_val = 0;
    pcnt_unit_get_count(unit, &current_val);

    z_count_at_last_mark = current_val;
    z_marks_detected++;
    */
    int current_val = 0;
    pcnt_unit_get_count((pcnt_unit_handle_t)arg, &current_val);
    z_count_at_last_mark = current_val + (int)total_pulses; // Фиксируем точный ABS в момент Z
    z_marks_detected++;
}

//void init_encoder() {
// Изменим функцию, чтобы она возвращала указатель на юнит, иначе мы не сможем читать его в main
pcnt_unit_handle_t init_encoder() {

    ESP_LOGI(TAG, "Инициализация PCNT для энкодера %d P/R...", ENCODER_LINES);

    /*
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ENCODER_A_PIN) | (1ULL << ENCODER_B_PIN) | (1ULL << ENCODER_Z_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,   // ОТКЛЮЧАЕМ внутреннюю подтяжку
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    */

    // 1. Настройка пина Z через GPIO (для надежности)
    gpio_config_t z_pin_config = {
        .pin_bit_mask = (1ULL << ENCODER_Z_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, // Внешний 1к к 3.3В уже стоит
    };
    gpio_config(&z_pin_config);



    pcnt_unit_config_t unit_config = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit = PCNT_LOW_LIMIT,
    };

    pcnt_unit_handle_t pcnt_unit = NULL;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    // Настройка фильтра (антидребезг) - важно для механических энкодеров
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 12500, // игнорировать импульсы короче 1мкс
    };

    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    // Канал А
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ENCODER_A_PIN,
        .level_gpio_num = ENCODER_B_PIN,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    // Канал B (для квадратурного режима x4)
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = ENCODER_B_PIN,
        .level_gpio_num = ENCODER_A_PIN,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));

    // Установка действий (для классического инкрементального энкодера)
    // Канал А
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // Канал B
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_INVERSE, PCNT_CHANNEL_LEVEL_ACTION_KEEP));

    // 5. Добавление точек наблюдения
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, PCNT_HIGH_LIMIT));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, PCNT_LOW_LIMIT));

    // 6. Регистрация колбэков
    pcnt_event_callbacks_t cbs = {
        .on_reach = on_pcnt_reach_limit,
    };
    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit, &cbs, NULL));
    // -----------------------

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));


    // В конце функции init_encoder, перед pcnt_unit_start:
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(ENCODER_Z_PIN, encoder_z_isr_handler, pcnt_unit));
    ESP_ERROR_CHECK(gpio_set_intr_type(ENCODER_Z_PIN, GPIO_INTR_NEGEDGE)); // Сброс при замыкании на землю

    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
    ESP_LOGI(TAG, "PCNT успешно запущен");
    return pcnt_unit;

}

void app_main(void)
{
    /*
    // Инициализируем и получаем хендл юнита
    pcnt_unit_handle_t encoder_unit = init_encoder();

    int current_count = 0;

    while (1) {
        // Читаем значение счетчика
        ESP_ERROR_CHECK(pcnt_unit_get_count(encoder_unit, &current_count));

        // Выводим значение (каждые 100 мс)
        //ESP_LOGI(TAG, "Счетчик: %d", current_count);
        ESP_LOGI(TAG, "Сброс был на: %d, Текущий: %d", last_reset_at, current_count);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
    */

    pcnt_unit_handle_t encoder_unit = init_encoder();

    // Настройка прерывания на переполнение
    //pcnt_event_callbacks_t cbs = { .on_reach_limit = on_pcnt_reach_limit };
    //ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(encoder_unit, &cbs, NULL));






    // Настройка прерывания Z (пин 36)
    gpio_isr_handler_add(ENCODER_Z_PIN, encoder_z_isr_handler, encoder_unit);
    gpio_set_intr_type(ENCODER_Z_PIN, GPIO_INTR_NEGEDGE);

    int current_pcnt = 0;
    while (1) {
        pcnt_unit_get_count(encoder_unit, &current_pcnt);
        int64_t absolute_pos = total_pulses + current_pcnt;

        // Выводим: общую позицию и остаток от деления на 7200
        // Если система точна, то при каждом срабатывании Z (когда крутите рукой)
        // absolute_pos % 7200 должен быть всегда одним и тем же числом (например, 0 или 7199)
        //printf("ABS: %lld | Z_Detected: %ld | Modulo 7200: %lld\n",
        //        absolute_pos, z_marks_detected, absolute_pos % 7200);

        printf("Z_Pos: %ld | Modulo: %ld\n", z_count_at_last_mark, z_count_at_last_mark % 7200);


        vTaskDelay(pdMS_TO_TICKS(200));
    }
}






// Получение значения
//int get_count(pcnt_unit_handle_t unit) {
    //int pulse_count = 0;
    //pcnt_unit_get_count(unit, &pulse_count);
    //return pulse_count;
//}



//void setup_z_phase(pcnt_unit_handle_t pcnt_unit) {
    // Настраиваем событие: сброс при совпадении с нулем не нужен,
    // нам нужно внешнее управление через пин Z.

    // В новом PCNT API фаза Z настраивается через "Watch Points"
    // или прямое прерывание GPIO. Но правильнее для E6B2 использовать
    // очистку (clear) по сигналу.

    // Вариант 1: Использование Z как триггера прерывания для сброса

    //gpio_config_t io_conf = {
    //    .pin_bit_mask = (1ULL << ENCODER_Z_PIN),
    //    .mode = GPIO_MODE_INPUT,
    //    .pull_up_en = GPIO_PULLUP_DISABLE, // внешняя подтяжка 1к уже есть
    //    .intr_type = GPIO_INTR_NEGEDGE,    // срабатывание при замыкании на землю
    //};
    //gpio_config(&io_conf);

    // В обработчике прерывания (ISR) вызываем:
    //* pcnt_unit_clear_count(pcnt_unit);
//}


// В обработчике прерывания Z-метки (пин 36)
//static void IRAM_ATTR z_signal_handler(void* arg) {
//    pcnt_unit_handle_t unit = (pcnt_unit_handle_t) arg;
//    pcnt_unit_clear_count(unit); // Обнуляем для синхронизации начала витка
    // Здесь же можно подать сигнал задающему алгоритму (Bresenham или PID)
//}

