#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "SERVO_MG90S_CR";   // CR = Continuous Rotation

#define SERVO_PIN             18
#define SERVO_FREQ_HZ         50
#define SERVO_TIMER           LEDC_TIMER_0
#define SERVO_CHANNEL         LEDC_CHANNEL_0
#define SERVO_DUTY_RES        LEDC_TIMER_13_BIT

static inline uint32_t pulse_us_to_duty(uint32_t us)
{
    return (us * (1ULL << SERVO_DUTY_RES)) / 20000ULL;   // 20 ms period
}

void servo_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = SERVO_TIMER,
        .duty_resolution  = SERVO_DUTY_RES,
        .freq_hz          = SERVO_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

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

    ESP_LOGI(TAG, "Servo ready on GPIO%d (LEDC)", SERVO_PIN);
}

void servo_set_pulse(uint32_t pulse_us)
{
    if (pulse_us < 500)  pulse_us = 500;
    if (pulse_us > 2500) pulse_us = 2500;

    uint32_t duty = pulse_us_to_duty(pulse_us);

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL));

    ESP_LOGI(TAG, "Commanded pulse: %lu µs  |  Duty: %lu", pulse_us, duty);
}

void servo_set_speed(int8_t speed_percent)   // -100 .. +100
{
    uint32_t pulse_us = 1500;

    if (speed_percent > 0) {
        pulse_us = 1500 + (int32_t)(700 * speed_percent) / 100;
    } else if (speed_percent < 0) {
        pulse_us = 1500 + (int32_t)(700 * speed_percent) / 100;
    }
    // 0 → exactly 1500 µs (stop)

    servo_set_pulse(pulse_us);
    ESP_LOGI(TAG, "Speed: %+d %%", speed_percent);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== MG90S Continuous-Rotation Diagnosis (ESP32-S3-DevKitC-1) ===");
    ESP_LOGI(TAG, "Make sure servo has external 5V + 470µF cap!");

    servo_init();


    // 1. Positive direction test
    ESP_LOGI(TAG, "\n--- 1. 2000 µs (should rotate ONE direction) ---");
    servo_set_pulse(2000);
    vTaskDelay(pdMS_TO_TICKS(5000));


    // 2. Neutral / stop command
    ESP_LOGI(TAG, "\n--- 2. Neutral 1500 µs (should STOP or hold) ---");
    servo_set_pulse(1500);
    vTaskDelay(pdMS_TO_TICKS(10000));

    // 3. Opposite direction test
    ESP_LOGI(TAG, "\n--- 3. 1000 µs (should rotate OPPOSITE direction) ---");
    servo_set_pulse(1000);
    vTaskDelay(pdMS_TO_TICKS(5000));



    // 4. Back to stop + speed ramp demo
    ESP_LOGI(TAG, "\n--- 4. Back to stop ---");
    servo_set_speed(0);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Speed ramp test (0% → +80% → stop)...");
    for (int8_t s = 0; s <= 80; s += 10) {
        servo_set_speed(s);
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
    servo_set_speed(0);

    ESP_LOGI(TAG, "\n=== Diagnosis complete ===");
    ESP_LOGI(TAG, "Please reply with:");
    ESP_LOGI(TAG, "1. Does 1500 µs actually stop the servo (or very slow creep)?");
    ESP_LOGI(TAG, "2. At 2000 µs: which direction and how fast?");
    ESP_LOGI(TAG, "3. At 1000 µs: opposite direction?");
    ESP_LOGI(TAG, "4. Any loud buzzing or excessive heat at extremes?");
}