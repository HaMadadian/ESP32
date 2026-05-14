#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "SERVO_MG90S";

// MG90S calibration values — measure with logic analyzer in production!
#define SERVO_MIN_PULSE_US    520     // 0°   (tune these!)
#define SERVO_MAX_PULSE_US    2480    // 180° (many MG90S are ~2400-2500 µs)
#define SERVO_NEUTRAL_US      1500    // 90°
// static uint8_t angle = 0; // global variable to hold current angle for demo loop
#define SERVO_PIN             18      // GPIO18 on ESP32-S3-DevKitC-1
#define SERVO_FREQ_HZ         50
#define SERVO_TIMER           LEDC_TIMER_0
#define SERVO_CHANNEL         LEDC_CHANNEL_0
#define SERVO_DUTY_RES        LEDC_TIMER_13_BIT   // 8192 levels → ~0.24 µs resolution at 50 Hz

static inline uint32_t pulse_us_to_duty(uint32_t us)
{
    // Period in µs = 1 000 000 / 50 = 20 000 µs
    return (us * (1ULL << SERVO_DUTY_RES)) / 20000ULL;
}

void servo_init(void)
{
    // Timer configuration
    ledc_timer_config_t timer_conf = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = SERVO_TIMER,
        .duty_resolution  = SERVO_DUTY_RES,
        .freq_hz          = SERVO_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    // Channel configuration
    ledc_channel_config_t ch_conf = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = SERVO_CHANNEL,
        .timer_sel      = SERVO_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = SERVO_PIN,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_conf));

    ESP_LOGI(TAG, "MG90S servo ready on GPIO%d (LEDC ch %d)", SERVO_PIN, SERVO_CHANNEL);
}

void servo_set_angle(uint8_t angle_deg)
{
    if (angle_deg > 180) angle_deg = 180;

    uint32_t pulse_us = SERVO_MIN_PULSE_US +
                        ((SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * (uint32_t)angle_deg) / 180;

    uint32_t duty = pulse_us_to_duty(pulse_us);

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL));

    ESP_LOGI(TAG, "Set angle %u° → pulse %lu µs → duty %lu", angle_deg, pulse_us, duty);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3-DevKitC-1 MG90S Servo Control (Production LEDC) ===");

    servo_init();
    angle = 0; // start at 0°
    // Demo: smooth sweep (production code would use commands instead)
    while (1) {
        servo_set_angle(angle);
        vTaskDelay(pdMS_TO_TICKS(1000));   // smooth motion
    }
    //     for (uint8_t angle = 0; angle <= 180; angle += 90) {
    //         servo_set_angle(angle);
    //         vTaskDelay(pdMS_TO_TICKS(1000));   // smooth motion
            
    //     }
    //     vTaskDelay(pdMS_TO_TICKS(100)); // hold at 180° for a moment

    //     for (uint8_t angle = 180; angle > 0; angle -= 90) {
    //         servo_set_angle(angle);
    //         vTaskDelay(pdMS_TO_TICKS(1000)); // smooth motion
    //     }
    //     vTaskDelay(pdMS_TO_TICKS(100)); // hold at 0° for a moment
    // }
}