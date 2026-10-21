/*
 * EtherCAT Test Functions
 * Симуляция EtherCAT фреймов для тестирования параллельной работы с Telnet
 * 
 * Phase 3, Week 6 - Performance Testing
 * ESP32 EtherCAT Slave v4
 */

#ifndef ETHERCAT_TEST_H
#define ETHERCAT_TEST_H

#include "ethercat_type.h"
#include "w5500_macraw.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Test Statistics Structure
// ============================================================================

typedef struct {
    uint32_t frames_sent;          /**< Количество отправленных фреймов */
    uint32_t frames_received;       /**< Количество полученных фреймов */
    uint32_t frames_dropped;        /**< Количество потерянных фреймов */
    uint32_t telnet_commands;      /**< Количество обработанных Telnet команд */
    
    uint32_t min_latency_us;        /**< Минимальная задержка (микросекунды) */
    uint32_t max_latency_us;        /**< Максимальная задержка (микросекунды) */
    uint64_t total_latency_us;      /**< Общая задержка для расчета среднего */
    uint32_t latency_samples;       /**< Количество измерений задержки */
    
    uint32_t cpu_load_percent;      /**< Загрузка CPU (%) */
    uint32_t heap_free_min;          /**< Минимальный свободный heap */
    
    uint32_t test_duration_ms;     /**< Длительность теста (мс) */
    uint32_t start_time_ms;         /**< Время начала теста */
    
} ethercat_test_stats_t;

// ============================================================================
// Test Configuration
// ============================================================================

typedef struct {
    boolean enabled;                /**< Включен ли тест */
    uint32_t frame_interval_us;     /**< Интервал между фреймами (микросекунды) */
    uint32_t test_duration_sec;     /**< Длительность теста (секунды) */
    uint8_t frame_type;             /**< Тип фрейма (0=APRD, 1=APWR, 2=LRW) */
    boolean measure_latency;        /**< Измерять задержку */
    boolean parallel_telnet;        /**< Работать параллельно с Telnet */
    boolean loopback_mode;          /**< Loopback режим (фреймы возвращаются обратно) */
    
} ethercat_test_config_t;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Инициализация тестового модуля
 * @param w5500_in Указатель на W5500 IN (для приема ответов)
 * @param w5500_out Указатель на W5500 OUT (для отправки фреймов)
 * @param ethercat_stack Указатель на EtherCAT Stack (для loopback режима, может быть NULL)
 * @return true если успешно
 */
boolean ethercat_test_init(w5500_macraw_t* w5500_in, w5500_macraw_t* w5500_out, void* ethercat_stack);

/**
 * @brief Начать тест симуляции EtherCAT фреймов
 * @param config Конфигурация теста
 * @return true если успешно
 */
boolean ethercat_test_start(const ethercat_test_config_t* config);

/**
 * @brief Остановить тест
 */
void ethercat_test_stop(void);

/**
 * @brief Обработка теста (вызывать в loop)
 */
void ethercat_test_process(void);

/**
 * @brief Получить статистику теста
 * @param stats Указатель на структуру статистики
 */
void ethercat_test_get_stats(ethercat_test_stats_t* stats);

/**
 * @brief Сброс статистики
 */
void ethercat_test_reset_stats(void);

/**
 * @brief Создать тестовый EtherCAT фрейм
 * @param frame Буфер для фрейма
 * @param frame_type Тип фрейма (0=APRD, 1=APWR, 2=LRW)
 * @param address Адрес для чтения/записи
 * @param data Данные для записи (для APWR/LRW)
 * @return Длина фрейма в байтах
 */
uint16_t ethercat_test_create_frame(uint8_t* frame, uint8_t frame_type, 
                                    uint16_t address, uint16_t data);

#ifdef __cplusplus
}
#endif

#endif // ETHERCAT_TEST_H
