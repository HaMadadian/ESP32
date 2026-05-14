#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include <stdlib.h>        // for atoi
#include <string.h>        // for strcspn

static const char *TAG = "SERVO_CR";

#define SERVO_PIN             18
#define SERVO_FREQ_HZ         50
#define UART_PORT             UART_NUM_0
#define CR_NEUTRAL_US         1500
#define CR_SPEED_RANGE_US     700      // Tune this if max speed is too aggressive
#define COMMAND_TIMEOUT_MS    8000     // Auto-stop after 8 seconds of no command

static uint32_t last_command_time = 0;

static inline uint32_t pulse_us_to_duty(uint32_t us)
{
    return (us * (1ULL << LEDC_TIMER_13_BIT)) / 20000ULL;
}

void servo_init(void)
{
    // === LEDC Setup ===
    ledc_timer_config_t timer_conf = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_13_BIT,
        .freq_hz          = SERVO_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t ch_conf = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = SERVO_PIN,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_conf));

    ESP_LOGI(TAG, "Continuous-rotation MG90S initialized on GPIO%d", SERVO_PIN);
}

void servo_set_pulse(uint32_t pulse_us)
{
    if (pulse_us < 800)  pulse_us = 800;
    if (pulse_us > 2200) pulse_us = 2200;

    uint32_t duty = pulse_us_to_duty(pulse_us);

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));

    ESP_LOGI(TAG, "Pulse → %lu µs", pulse_us);
}

void servo_set_speed(int8_t speed_percent)
{
    last_command_time = esp_timer_get_time() / 1000;

    uint32_t pulse_us = CR_NEUTRAL_US;

    if (speed_percent > 0) {
        pulse_us = CR_NEUTRAL_US + (CR_SPEED_RANGE_US * (uint32_t)speed_percent) / 100;
    } else if (speed_percent < 0) {
        pulse_us = CR_NEUTRAL_US + (CR_SPEED_RANGE_US * (int32_t)speed_percent) / 100;
    }

    servo_set_pulse(pulse_us);
    ESP_LOGI(TAG, "Speed command: %+d%%", speed_percent);
}

void servo_stop(void)
{
    servo_set_pulse(CR_NEUTRAL_US);
    ESP_LOGI(TAG, "Servo stopped (timeout or manual stop)");
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Production MG90S Continuous-Rotation Servo with UART Control ===");
    ESP_LOGI(TAG, "Commands: send integer from -100 to +100 (e.g. 45, -30, 0)");

    servo_init();
    servo_stop();

    int8_t current_speed = 0;

    // === Proper UART Configuration (Production Standard) ===
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 1024 * 2, 0, 0, NULL, 0));

    char buf[32];

    while (1) {
        uint32_t now = esp_timer_get_time() / 1000;

        // Safety timeout
        if (now - last_command_time > COMMAND_TIMEOUT_MS) {
            if (current_speed != 0) {
                servo_stop();
                current_speed = 0;
            }
        }

        // Read UART with timeout
        int len = uart_read_bytes(UART_PORT, (uint8_t*)buf, sizeof(buf) - 1, pdMS_TO_TICKS(50));

        if (len > 0) {
            buf[len] = '\0';

            // Clean input: remove \r and \n
            buf[strcspn(buf, "\r\n")] = '\0';

            int speed = atoi(buf);

            if (speed < -100) speed = -100;
            if (speed > 100)  speed = 100;

            servo_set_speed((int8_t)speed);
            current_speed = (int8_t)speed;

            // Echo back for easier debugging
            ESP_LOGI(TAG, "Applied speed: %+d%%", speed);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}