#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "DS1302";

// Pin definitions
#define DS1302_CE    GPIO_NUM_4
#define DS1302_IO    GPIO_NUM_5
#define DS1302_SCLK  GPIO_NUM_6

#define SET_TIME_NOW  0   // ← Change to 1 only when you want to force time set

// BCD helpers
static uint8_t bcd_to_dec(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}
static uint8_t dec_to_bcd(uint8_t dec) {
    return ((dec / 10) << 4) | (dec % 10);
}

// Low-level bit-bang (unchanged - good)
static void ds1302_send_byte(uint8_t value) {
    gpio_set_direction(DS1302_IO, GPIO_MODE_OUTPUT);
    for (int i = 0; i < 8; i++) {
        gpio_set_level(DS1302_IO, value & 0x01);
        value >>= 1;
        gpio_set_level(DS1302_SCLK, 1);
        gpio_set_level(DS1302_SCLK, 0);
    }
}

static uint8_t ds1302_read_byte(void) {
    gpio_set_direction(DS1302_IO, GPIO_MODE_INPUT);
    uint8_t value = 0;
    for (int i = 0; i < 8; i++) {
        gpio_set_level(DS1302_SCLK, 1);
        if (gpio_get_level(DS1302_IO)) value |= (1 << i);
        gpio_set_level(DS1302_SCLK, 0);
    }
    return value;
}

// High-level API
static void ds1302_write(uint8_t cmd, uint8_t data) {
    gpio_set_level(DS1302_CE, 1);
    ds1302_send_byte(cmd);
    ds1302_send_byte(data);
    gpio_set_level(DS1302_CE, 0);
}

static uint8_t ds1302_read(uint8_t cmd) {
    gpio_set_level(DS1302_CE, 1);
    ds1302_send_byte(cmd);
    uint8_t data = ds1302_read_byte();
    gpio_set_level(DS1302_CE, 0);
    return data;
}

typedef struct {
    uint8_t sec, min, hour, day, month, year;  // BCD
} ds1302_time_t;

// Global default time (used only when SET_TIME_NOW == 1 or RTC is corrupt)
static const ds1302_time_t default_time = {
    .sec   = 0x00,   // BCD already
    .min   = 0x14,
    .hour  = 0x22,
    .day   = 0x12,
    .month = 0x04,
    .year  = 0x26
};

static void ds1302_set_time(const ds1302_time_t *t)
{
    ds1302_write(0x8E, 0x00);                    // Clear Write Protect

    uint8_t sec = t->sec & 0x7F;                  // Force CH = 0
    ds1302_write(0x80, sec);
    ds1302_write(0x82, t->min);
    ds1302_write(0x84, t->hour & 0x7F);          // Force 24h mode
    ds1302_write(0x86, t->day);
    ds1302_write(0x88, t->month);
    ds1302_write(0x8C, t->year);

    ds1302_write(0x8A, 0x00);                    // Control register

    ESP_LOGI(TAG, "DS1302 time set successfully");
}

static bool ds1302_get_time(ds1302_time_t *t)
{
    t->sec   = ds1302_read(0x81);
    t->min   = ds1302_read(0x83);
    t->hour  = ds1302_read(0x85);
    t->day   = ds1302_read(0x87);
    t->month = ds1302_read(0x89);
    t->year  = ds1302_read(0x8D);

    // Validation
    if ((t->sec & 0x80) || (t->sec > 0x59) || (t->min > 0x59) ||
        ((t->hour & 0x7F) > 0x23) || (t->day > 0x31) ||
        (t->month > 0x12) || (t->year > 0x99)) {

        ESP_LOGE(TAG, "RTC data corrupt or halted! Re-initializing...");
        ds1302_set_time(&default_time);
        return false;
    }
    return true;
}


static void ds1302_print_battery_status(void) {
    uint8_t trickle = ds1302_read(0x8F);   // TCS register read
    ESP_LOGI(TAG, "Trickle charge register = 0x%02X (should be 0x00 or 0xA0)", trickle);
}

static void ds1302_check_oscillator(void)
{
    uint8_t sec = ds1302_read(0x81);
    if (sec & 0x80) {
        ESP_LOGE(TAG, "Clock Halt bit is SET! Oscillator stopped.");
    } else {
        ESP_LOGI(TAG, "RTC oscillator is running (CH=0)");
    }
}

static void ds1302_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL<<DS1302_CE) | (1ULL<<DS1302_IO) | (1ULL<<DS1302_SCLK),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(DS1302_CE, 0);
    gpio_set_level(DS1302_SCLK, 0);

    ds1302_write(0x8E, 0x00);   // Clear WP early
    ds1302_check_oscillator();
    ds1302_print_battery_status();

    ESP_LOGI(TAG, "DS1302 initialized");
}



void app_main(void)
{   
    vTaskDelay(pdMS_TO_TICKS(5000));
    ds1302_init();

    #if SET_TIME_NOW
        ds1302_set_time(&default_time);
    #endif

    while (1) {
        ds1302_time_t t;
        ds1302_get_time(&t);           // validation happens inside

        ESP_LOGI(TAG, "20%02d-%02d-%02d %02d:%02d:%02d",
                 bcd_to_dec(t.year),
                 bcd_to_dec(t.month),
                 bcd_to_dec(t.day),
                 bcd_to_dec(t.hour & 0x3F),
                 bcd_to_dec(t.min),
                 bcd_to_dec(t.sec));

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}