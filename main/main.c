/*
 * Example of using esp-homekit library to control
 * a Sonoff S20 using HomeKit.
 * The esp-wifi-config library is also used in this
 * example. This means you don't have to specify
 * your network's SSID and password before building.
 *
 * In order to flash the sonoff S20 you will have to
 * have a 3,3v (logic level) FTDI adapter.
 *
 * To flash this example connect 3,3v, TX, RX, GND
 * in this order, beginning in the (square) pin header
 * Next hold down the button and connect the FTDI adapter
 * to your computer. The sonoff is now in flash mode and
 * you can flash the custom firmware.
 *
 * WARNING: Do not connect the sonoff to AC while it's
 * connected to the FTDI adapter! This may fry your
 * computer and sonoff.
 *
 */

#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <espressif/esp_common.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <wifi_config.h>
#include <button.h>
#include <led_status.h>
#include <ota-tftp.h>

#include "config.h"

// The GPIO pin that is connected to the relay on the Sonoff S26
const int relay_gpio = RELAY_GPIO;
// The GPIO pin that is connected to the LED on the Sonoff S26
const int led_gpio = LED_GPIO;
// The GPIO pin that is oconnected to the button on the Sonoff S26
const int button_gpio = BUTTON_GPIO;


// one short blink every 3 seconds
led_status_pattern_t mode_normal = LED_STATUS_PATTERN({100, -2900});
// two short blinks every 3 seconds
led_status_pattern_t mode_connecting_to_wifi = LED_STATUS_PATTERN({100, -100, 100, -2700});
// long blink, long wait
led_status_pattern_t mode_no_wifi_config = LED_STATUS_PATTERN({2000, -2000});
// short blink, long blink, long wait
led_status_pattern_t mode_unpaired = LED_STATUS_PATTERN({100, -100, 800, -1000});

// three short blinks
led_status_pattern_t mode_reset = LED_STATUS_PATTERN({100, -100, 100, -100, 100, -4500});
// three series of two short blinks
led_status_pattern_t mode_identify = LED_STATUS_PATTERN({ 100, -100, 100, -350, 100, -100, 100, -350, 100, -100, 100, -2500});

static led_status_t status;


void switch_on_callback(homekit_accessory_t *acc, homekit_characteristic_t *_ch, homekit_value_t on, void *context);
void button_callback(button_event_t event, void *_context);

void relay_write(bool on) {
    gpio_write(relay_gpio, on ? 1 : 0);
}

void reset_configuration_task() {
    // Flash the LED first before we start the reset
    led_status_signal(status, &mode_reset);
    vTaskDelay(500 / portTICK_PERIOD_MS);

    printf("Resetting Wifi Config\n");
    wifi_config_reset();

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    printf("Resetting HomeKit Config\n");
    homekit_server_reset();

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    printf("Restarting\n");
    sdk_system_restart();

    vTaskDelete(NULL);
}

void reset_configuration() {
    printf("Resetting Sonoff configuration\n");
    xTaskCreate(reset_configuration_task, "Reset configuration", 256, NULL, 2, NULL);
}

homekit_characteristic_t switch_on = HOMEKIT_CHARACTERISTIC_(
    ON, false, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(switch_on_callback)
);

void relay_init() {
    gpio_enable(relay_gpio, GPIO_OUTPUT);
    relay_write(switch_on.value.bool_value);
}

void switch_on_callback(homekit_accessory_t *acc, homekit_characteristic_t *_ch, homekit_value_t on, void *context) {
    relay_write(switch_on.value.bool_value);
}

void button_callback(button_event_t event, void *_context) {
    switch (event) {
        case button_event_single_press:
            printf("Toggling relay\n");
            switch_on.value.bool_value = !switch_on.value.bool_value;
            relay_write(switch_on.value.bool_value);
            homekit_characteristic_notify(&switch_on, switch_on.value);
            break;

        case button_event_double_press:
            printf("Restarting\n");
            sdk_system_restart();
            break;

        case button_event_long_press:
            reset_configuration();
            break;

        default:
            printf("Unknown button event: %d\n", event);
    }
}

void switch_identify(homekit_value_t _value) {
    printf("Switch identify\n");
    led_status_signal(status, &mode_identify);
}

homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, "Sonoff Outlet");

homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_outlet, .services=(homekit_service_t*[]){
        HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]){
            &name,
            HOMEKIT_CHARACTERISTIC(MANUFACTURER, "iTEAD"),
            HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "037A2BABF19E"),
            HOMEKIT_CHARACTERISTIC(MODEL, "S26"),
            HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.1.6"),
            HOMEKIT_CHARACTERISTIC(IDENTIFY, switch_identify),
            NULL
        }),
        HOMEKIT_SERVICE(OUTLET, .primary=true, .characteristics=(homekit_characteristic_t*[]){
            HOMEKIT_CHARACTERISTIC(NAME, "Sonoff Outlet"),
            &switch_on,
            HOMEKIT_CHARACTERISTIC(OUTLET_IN_USE, true),
            NULL
        }),
        NULL
    }),
    NULL
};

void on_homekit_event(homekit_event_t event) {
    if (event == HOMEKIT_EVENT_PAIRING_ADDED) {
        led_status_set(status, &mode_normal);
    } else if (event == HOMEKIT_EVENT_PAIRING_REMOVED) {
        if (!homekit_is_paired()) {
            led_status_set(status, &mode_unpaired);
        }
    }
}

static bool initialized = false;
homekit_server_config_t config = {
    .accessories = accessories,
    .password = ACCESSORY_SETUP_CODE,
    .on_event = on_homekit_event,
};

void on_wifi_config_event(wifi_config_event_t event) {
    if (event == WIFI_CONFIG_CONNECTED && !initialized) {
        if (!initialized) {
            homekit_server_init(&config);
            ota_tftp_init_server(TFTP_PORT);

            initialized = true;

            led_status_set(status, homekit_is_paired() ? &mode_normal : &mode_unpaired);
        }
    }
}

void create_accessory_name() {
    uint8_t macaddr[6];
    sdk_wifi_get_macaddr(STATION_IF, macaddr);

    int name_len = snprintf(NULL, 0, ACCESSORY_NAME "-%02X%02X%02X",
                            macaddr[3], macaddr[4], macaddr[5]);
    char *name_value = malloc(name_len+1);
    snprintf(name_value, name_len+1, ACCESSORY_NAME "-%02X%02X%02X",
             macaddr[3], macaddr[4], macaddr[5]);

    name.value = HOMEKIT_STRING(name_value);
}

bool wifi_is_configured() {
    char *ssid = NULL;
    wifi_config_get(&ssid, NULL);
    if (!ssid)
        return false;

    free(ssid);
    return true;
}

void user_init(void) {
    uart_set_baud(0, 115200);

    relay_init();
    status = led_status_init(led_gpio, LED_ACTIVE_LEVEL);

    create_accessory_name();

    button_config_t button_config = BUTTON_CONFIG(
        (BUTTON_ACTIVE_LEVEL) ? button_active_high : button_active_low,
        .max_repeat_presses=2,
        .long_press_time=5000,
    );
    if (button_create(button_gpio, button_config, button_callback, NULL)) {
        printf("Failed to initialize button\n");
    }

    wifi_config_init2(WIFI_AP_NAME, WIFI_AP_PASSWORD, on_wifi_config_event);

    led_status_set(status, wifi_is_configured() ? &mode_connecting_to_wifi : &mode_no_wifi_config);
}
