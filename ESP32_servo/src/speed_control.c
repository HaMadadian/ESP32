#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"      // for timeout

static const char *TAG = "SERVO_CR";

#define SERVO_PIN             18
#define SERVO_FREQ_HZ         50
#define SERVO_TIMER           LEDC_TIMER_0
#define SERVO_CHANNEL         LEDC_CHANNEL_0
#define SERVO_DUTY_RES        LEDC_TIMER_13_BIT

#define CR_NEUTRAL_US         1500
#define CR_SPEED_RANGE_US     700     // tune this value if max speed feels too fast/slow
// define boolean flag to track increasing or reducing speed in demo loop
static bool speed_increasing = true;

static uint32_t last_command_time = 0;

static inline uint32_t pulse_us_to_duty(uint32_t us)
{
    return (us * (1ULL << SERVO_DUTY_RES)) / 20000ULL;
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

    ESP_LOGI(TAG, "Continuous-rotation MG90S ready on GPIO%d", SERVO_PIN);
}

void servo_set_pulse(uint32_t pulse_us)
{
    if (pulse_us < 800)  pulse_us = 800;
    if (pulse_us > 2200) pulse_us = 2200;

    uint32_t duty = pulse_us_to_duty(pulse_us);

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL));

    ESP_LOGI(TAG, "Pulse: %lu µs", pulse_us);
}

void servo_set_speed(int8_t speed_percent)
{
    last_command_time = esp_timer_get_time() / 1000;   // ms

    uint32_t pulse_us = CR_NEUTRAL_US;

    if (speed_percent > 0) {
        pulse_us = CR_NEUTRAL_US + (CR_SPEED_RANGE_US * (uint32_t)speed_percent) / 100;
    } else if (speed_percent < 0) {
        pulse_us = CR_NEUTRAL_US + (CR_SPEED_RANGE_US * (int32_t)speed_percent) / 100;
    }

    servo_set_pulse(pulse_us);
    ESP_LOGI(TAG, "Speed: %+d%%", speed_percent);
}

void servo_stop(void)
{
    servo_set_pulse(CR_NEUTRAL_US);
    ESP_LOGI(TAG, "Servo stopped (timeout or manual stop)");
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Production Continuous-Rotation MG90S Driver ===");
    ESP_LOGI(TAG, "External 5V + bulk capacitor required!");

    servo_init();
    servo_stop();                       // start in safe state

    int8_t current_speed = 0;
    uint32_t last_ramp_time = 0;

    while (1) {
        uint32_t now = esp_timer_get_time() / 1000;

        // Safety timeout: if no command for 5 seconds → stop
        if (now - last_command_time > 5000) {
            if (current_speed != 0) {
                servo_stop();
                current_speed = 0;
            }
        }

        // Simple demo: ramp speed up and down smoothly
        if (now - last_ramp_time > 1500) {
            last_ramp_time = now;

            if (speed_increasing && current_speed < 90) {
                current_speed += 10;
                if (current_speed >= 90) {
                    speed_increasing = false; // start decreasing after reaching +90%
                }
            } else if (speed_increasing == false && current_speed > -90) {
                current_speed -= 10;   // go negative to show both directions
                if (current_speed <= -90) {
                    speed_increasing = true; // start increasing after reaching -90%
                }
            } else {
                current_speed = 0;
            }

            servo_set_speed(current_speed);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}