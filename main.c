#include "pico/stdlib.h"
#include "tusb.h"
#include "hardware/structs/sio.h"

//--------------------------------------------------------------------
// GPIO Wiring — GP8–GP12 (avoids GP13 onboard LED on Feather RP2040)
//--------------------------------------------------------------------

// DB9 pins → RP2040 GPIO mapping
static const uint8_t PIN_UP    = 8;  // DB9 pin 1
static const uint8_t PIN_DOWN  = 9;  // DB9 pin 2
static const uint8_t PIN_LEFT  = 10;  // DB9 pin 3
static const uint8_t PIN_RIGHT = 11;   // DB9 pin 4
static const uint8_t PIN_FIRE  = 12;   // DB9 pin 6
// DB9 pin 8 → any Pico GND

// Pins are contiguous GP8–GP12 for easy extraction from SIO->GPIO_IN

#define LED_PIN 13

//--------------------------------------------------------------------
// Hat switch encoding
// Values 0-7 map to 8 compass directions, 8 = centered/null
// Reference:
//   0=Up  1=Up-Right  2=Right  3=Down-Right
//   4=Down  5=Down-Left  6=Left  7=Up-Left  8=Centered
//--------------------------------------------------------------------

#define HAT_CENTERED  8

//--------------------------------------------------------------------
// Lookup Table — binary literals for readability
// Index format (after extraction):
//   bit 0 = UP    (GP12)
//   bit 1 = DOWN  (GP11)
//   bit 2 = LEFT  (GP10)
//   bit 3 = RIGHT (GP9)
//   bit 4 = FIRE  (GP8)
//
// Report byte layout:
//   bits [3:0] = hat switch (0-8, where 8 = centered)
//   bit  [4]   = fire button (1 = pressed)
//   bits [7:5] = padding (always 0)
//   Format: [ppp f hhhh]
//--------------------------------------------------------------------

static const uint8_t report_lut[32] = {
    // --- Fire released (bit 4 = 0) ---
    // [ppp f hhhh]
    0b00001000,  // idx 0:  centered, no fire
    0b00000000,  // idx 1:  up, no fire
    0b00000100,  // idx 2:  down, no fire
    0b00001000,  // idx 3:  up+down (invalid) → centered
    0b00000110,  // idx 4:  left, no fire
    0b00000111,  // idx 5:  up-left, no fire
    0b00000101,  // idx 6:  down-left, no fire
    0b00001000,  // idx 7:  up+down+left (invalid) → centered
    0b00000010,  // idx 8:  right, no fire
    0b00000001,  // idx 9:  up-right, no fire
    0b00000011,  // idx 10: down-right, no fire
    0b00001000,  // idx 11: up+down+right (invalid) → centered
    0b00001000,  // idx 12: left+right (invalid) → centered
    0b00001000,  // idx 13: up+left+right (invalid) → centered
    0b00001000,  // idx 14: down+left+right (invalid) → centered
    0b00001000,  // idx 15: all dirs (invalid) → centered

    // --- Fire pressed (bit 4 = 1) ---
    // [ppp f hhhh]
    0b00011000,  // idx 16: centered + fire
    0b00010000,  // idx 17: up + fire
    0b00010100,  // idx 18: down + fire
    0b00011000,  // idx 19: up+down (invalid) → centered + fire
    0b00010110,  // idx 20: left + fire
    0b00010111,  // idx 21: up-left + fire
    0b00010101,  // idx 22: down-left + fire
    0b00011000,  // idx 23: up+down+left (invalid) → centered + fire
    0b00010010,  // idx 24: right + fire
    0b00010001,  // idx 25: up-right + fire
    0b00010011,  // idx 26: down-right + fire
    0b00011000,  // idx 27: up+down+right (invalid) → centered + fire
    0b00011000,  // idx 28: left+right (invalid) → centered + fire
    0b00011000,  // idx 29: up+left+right (invalid) → centered + fire
    0b00011000,  // idx 30: down+left+right (invalid) → centered + fire
    0b00011000   // idx 31: all dirs (invalid) → centered + fire
};

//--------------------------------------------------------------------
// Pin initialization
//--------------------------------------------------------------------

static void init_pins(void) {
    gpio_init(PIN_UP);
    gpio_init(PIN_DOWN);
    gpio_init(PIN_LEFT);
    gpio_init(PIN_RIGHT);
    gpio_init(PIN_FIRE);

    gpio_set_dir(PIN_UP, GPIO_IN);
    gpio_set_dir(PIN_DOWN, GPIO_IN);
    gpio_set_dir(PIN_LEFT, GPIO_IN);
    gpio_set_dir(PIN_RIGHT, GPIO_IN);
    gpio_set_dir(PIN_FIRE, GPIO_IN);

    // Internal pull-ups — switch shorts to GND when pressed
    gpio_pull_up(PIN_UP);
    gpio_pull_up(PIN_DOWN);
    gpio_pull_up(PIN_LEFT);
    gpio_pull_up(PIN_RIGHT);
    gpio_pull_up(PIN_FIRE);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);
}

//--------------------------------------------------------------------
// Button reading and report packing
//--------------------------------------------------------------------

// Atomic read — all five pins sampled from same register read at same instant
static uint8_t build_report(void) {
    // Read all GPIO in one shot, invert (active low), extract 5 bits
    // Pins GP8–GP12 are in bits 8–12 of SIO->GPIO_IN
    uint32_t raw = ~sio_hw->gpio_in;
    uint8_t index = (raw >> 8) & 0x1F;
    return report_lut[index];
}

//--------------------------------------------------------------------
// Debouncing
//--------------------------------------------------------------------

static uint8_t last_reported = 0xFF;       // Force initial send by making it impossible
static uint8_t candidate_state = 0;
static uint32_t last_change_time = 0;

#define DEBOUNCE_MS 10

static bool poll_and_debounce(uint8_t *out_report) {
    uint8_t current = build_report();

    if (current != candidate_state) {
        // Raw state changed — restart debounce timer
        candidate_state = current;
        last_change_time = to_ms_since_boot(get_absolute_time());
        return false;  // Not stable yet
    }

    // State hasn't changed since last poll — check if it's been stable long enough
    if (candidate_state != last_reported &&
        (to_ms_since_boot(get_absolute_time()) - last_change_time) >= DEBOUNCE_MS) {
        // Stable for DEBOUNCE_MS — report it
        last_reported = candidate_state;
        *out_report = candidate_state;
        return true;
    }

    return false;
}

//--------------------------------------------------------------------
// HID report sending
//--------------------------------------------------------------------

static void send_hid_report(uint8_t report_byte) {
    if (tud_hid_ready()) {
        tud_hid_report(0, &report_byte, sizeof(report_byte));
    }
}

//--------------------------------------------------------------------
// Required TinyUSB HID callbacks (stubs — we don't use these)
//--------------------------------------------------------------------

// Called when host requests a report from us. We don't store reports
// from the host, so just return 0.
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

// Called when host sends a report to us (e.g., turning on LEDs).
// A joystick has no output, so we ignore it.
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

//--------------------------------------------------------------------
// Main loop
//--------------------------------------------------------------------

int main(void) {
    stdio_init_all();
    tusb_init();
    init_pins();

    // Wait for USB to be ready, then force an initial report
    while (!tud_mounted()) {
        tud_task();
        tight_loop_contents();
    }

    uint8_t initial_report = build_report();
    // Small delay to let the host settle after enumeration
    sleep_ms(50);
    send_hid_report(initial_report);

    while (true) {
        tud_task();  // TinyUSB event loop — handles enumeration, etc.

        uint8_t report;
        if (poll_and_debounce(&report)) {
            send_hid_report(report);
        }
//LED Debug
        if (report != 0x08) {
            gpio_put(LED_PIN, 1);
        } else {
            gpio_put(LED_PIN, 0);
        }
        tight_loop_contents();
    }
}
