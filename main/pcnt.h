#pragma once

#include <stdint.h>
#include "driver/pulse_cnt.h"

// Делаем переменные видимыми для других файлов
// extern говорит компилятору, что сами переменные лежат в pcnt.c
extern volatile int64_t total_pulses;
extern volatile int64_t z_pos_absolute;

/**
 * @brief Инициализация энкодера через PCNT
 * @return pcnt_unit_handle_t Хендл созданного юнита для управления
 */
pcnt_unit_handle_t init_encoder(void);

/**
 * @brief Безопасное чтение абсолютного значения импульсов (64 бит)
 * @param unit Хендл юнита PCNT
 * @return int64_t Общее кол-во импульсов
 */
int64_t get_encoder_total_pulses(pcnt_unit_handle_t unit);


