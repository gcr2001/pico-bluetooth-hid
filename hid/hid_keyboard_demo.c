/*
 * Copyright (C) 2014 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL BLUEKITCHEN
 * GMBH OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at 
 * contact@bluekitchen-gmbh.com
 *
 */

#define BTSTACK_FILE__ "hid_keyboard_demo.c"
 
// *****************************************************************************
/* EXAMPLE_START(hid_keyboard_demo): HID Keyboard Classic with GPIO buttons
 *
 * @text This HID Device example demonstrates how to implement
 * an HID keyboard using GPIO buttons connected to a Raspberry Pi Pico W.
 * GP10 = 'd', GP11 = 'w', GP21 = 'a', GP20 = 's', GP15 = Status LED
 */
// *****************************************************************************


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "btstack.h"

// Add Pico SDK GPIO libraries
#include "hardware/gpio.h"
#include "pico/stdlib.h"

// timing of keypresses
#define TYPING_KEYDOWN_MS  20
#define TYPING_DELAY_MS    20

// Button debounce time in milliseconds
#define DEBOUNCE_MS 300

// GPIO pins for buttons
#define GPIO_BUTTON_D 10  // D key
#define GPIO_BUTTON_W 11  // W key
#define GPIO_BUTTON_A 21  // A key
#define GPIO_BUTTON_S 20  // S key

// GPIO pin for status LED
#define GPIO_STATUS_LED 15  // Status LED

// When not set to 0xffff, sniff and sniff subrating are enabled
static uint16_t host_max_latency = 1600;
static uint16_t host_min_timeout = 3200;

#define REPORT_ID 0x01

// close to USB HID Specification 1.1, Appendix B.1
const uint8_t hid_descriptor_keyboard[] = {

    0x05, 0x01,                    // Usage Page (Generic Desktop)
    0x09, 0x06,                    // Usage (Keyboard)
    0xa1, 0x01,                    // Collection (Application)

    // Report ID

    0x85, REPORT_ID,               // Report ID

    // Modifier byte (input)

    0x75, 0x01,                    //   Report Size (1)
    0x95, 0x08,                    //   Report Count (8)
    0x05, 0x07,                    //   Usage Page (Key codes)
    0x19, 0xe0,                    //   Usage Minimum (Keyboard LeftControl)
    0x29, 0xe7,                    //   Usage Maximum (Keyboard Right GUI)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x25, 0x01,                    //   Logical Maximum (1)
    0x81, 0x02,                    //   Input (Data, Variable, Absolute)

    // Reserved byte (input)

    0x75, 0x01,                    //   Report Size (1)
    0x95, 0x08,                    //   Report Count (8)
    0x81, 0x03,                    //   Input (Constant, Variable, Absolute)

    // LED report + padding (output)

    0x95, 0x05,                    //   Report Count (5)
    0x75, 0x01,                    //   Report Size (1)
    0x05, 0x08,                    //   Usage Page (LEDs)
    0x19, 0x01,                    //   Usage Minimum (Num Lock)
    0x29, 0x05,                    //   Usage Maximum (Kana)
    0x91, 0x02,                    //   Output (Data, Variable, Absolute)

    0x95, 0x01,                    //   Report Count (1)
    0x75, 0x03,                    //   Report Size (3)
    0x91, 0x03,                    //   Output (Constant, Variable, Absolute)

    // Keycodes (input)

    0x95, 0x06,                    //   Report Count (6)
    0x75, 0x08,                    //   Report Size (8)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x25, 0xff,                    //   Logical Maximum (1)
    0x05, 0x07,                    //   Usage Page (Key codes)
    0x19, 0x00,                    //   Usage Minimum (Reserved (no event indicated))
    0x29, 0xff,                    //   Usage Maximum (Reserved)
    0x81, 0x00,                    //   Input (Data, Array)

    0xc0,                          // End collection
};

// 
#define CHAR_ILLEGAL     0xff
#define CHAR_RETURN     '\n'
#define CHAR_ESCAPE      27
#define CHAR_TAB         '\t'
#define CHAR_BACKSPACE   0x7f

// Simplified US Keyboard with Shift modifier

/**
 * English (US)
 */
static const uint8_t keytable_us_none [] = {
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /*   0-3 */
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',                   /*  4-13 */
    'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',                   /* 14-23 */
    'u', 'v', 'w', 'x', 'y', 'z',                                       /* 24-29 */
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',                   /* 30-39 */
    CHAR_RETURN, CHAR_ESCAPE, CHAR_BACKSPACE, CHAR_TAB, ' ',            /* 40-44 */
    '-', '=', '[', ']', '\\', CHAR_ILLEGAL, ';', '\'', 0x60, ',',       /* 45-54 */
    '.', '/', CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,   /* 55-60 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 61-64 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 65-68 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 69-72 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 73-76 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 77-80 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 81-84 */
    '*', '-', '+', '\n', '1', '2', '3', '4', '5',                       /* 85-97 */
    '6', '7', '8', '9', '0', '.', 0xa7,                                 /* 97-100 */
}; 

static const uint8_t keytable_us_shift[] = {
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /*  0-3  */
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',                   /*  4-13 */
    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',                   /* 14-23 */
    'U', 'V', 'W', 'X', 'Y', 'Z',                                       /* 24-29 */
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',                   /* 30-39 */
    CHAR_RETURN, CHAR_ESCAPE, CHAR_BACKSPACE, CHAR_TAB, ' ',            /* 40-44 */
    '_', '+', '{', '}', '|', CHAR_ILLEGAL, ':', '"', 0x7E, '<',         /* 45-54 */
    '>', '?', CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,   /* 55-60 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 61-64 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 65-68 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 69-72 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 73-76 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 77-80 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 81-84 */
    '*', '-', '+', '\n', '1', '2', '3', '4', '5',                       /* 85-97 */
    '6', '7', '8', '9', '0', '.', 0xb1,                                 /* 97-100 */
}; 

// STATE

static uint8_t hid_service_buffer[300];
static uint8_t device_id_sdp_service_buffer[100];
static const char hid_device_name[] = "BTstack HID Keyboard";
static btstack_packet_callback_registration_t hci_event_callback_registration;
static uint16_t hid_cid;
static uint8_t hid_boot_device = 0;

// HID Report sending
static uint8_t                send_buffer_storage[16];
static btstack_ring_buffer_t  send_buffer;
static btstack_timer_source_t send_timer;
static uint8_t                send_modifier;
static uint8_t                send_keycode;
static bool                   send_active;

// GPIO button debounce timers
static uint32_t last_button_press_time[4] = {0, 0, 0, 0};

static bd_addr_t device_addr;
static const char * device_addr_string = "BC:EC:5D:E6:15:03"; // Target device address

static enum {
    APP_BOOTING,
    APP_NOT_CONNECTED,
    APP_CONNECTING,
    APP_CONNECTED
} app_state = APP_BOOTING;

// Function to update LED status based on connection state
static void update_status_led(void) {
    if (app_state == APP_CONNECTED) {
        gpio_put(GPIO_STATUS_LED, 1);  // Turn LED ON when connected
        printf("Status LED: ON (Connected)\n");
    } else {
        gpio_put(GPIO_STATUS_LED, 0);  // Turn LED OFF when not connected
        printf("Status LED: OFF (Disconnected)\n");
    }
}

// HID Keyboard lookup
static bool lookup_keycode(uint8_t character, const uint8_t * table, int size, uint8_t * keycode){
    int i;
    for (i=0;i<size;i++){
        if (table[i] != character) continue;
        *keycode = i;
        return true;
    }
    return false;
}

static bool keycode_and_modifer_us_for_character(uint8_t character, uint8_t * keycode, uint8_t * modifier){
    bool found;
    found = lookup_keycode(character, keytable_us_none, sizeof(keytable_us_none), keycode);
    if (found) {
        *modifier = 0;  // none
        return true;
    }
    found = lookup_keycode(character, keytable_us_shift, sizeof(keytable_us_shift), keycode);
    if (found) {
        *modifier = 2;  // shift
        return true;
    }
    return false;
}

static void send_report(int modifier, int keycode){
    // setup HID message: A1 = Input Report, Report ID, Payload
    uint8_t message[] = {0xa1, REPORT_ID, modifier, 0, keycode, 0, 0, 0, 0, 0};
    hid_device_send_interrupt_message(hid_cid, &message[0], sizeof(message));
}

static void trigger_key_up(btstack_timer_source_t * ts){
    UNUSED(ts);
    hid_device_request_can_send_now_event(hid_cid);
}

static void send_next(btstack_timer_source_t * ts) {
    // get next key from buffer
    uint8_t character;
    uint32_t num_bytes_read = 0;
    btstack_ring_buffer_read(&send_buffer, &character, 1, &num_bytes_read);
    if (num_bytes_read == 0) {
        // buffer empty, nothing to send
        send_active = false;
    } else {
        send_active = true;
        // lookup keycode and modifier using US layout
        bool found = keycode_and_modifer_us_for_character(character, &send_keycode, &send_modifier);
        if (found) {
            // request can send now
            hid_device_request_can_send_now_event(hid_cid);
        } else {
            // restart timer for next character
            btstack_run_loop_set_timer(ts, TYPING_DELAY_MS);
            btstack_run_loop_add_timer(ts);
        }
    }
}

static void queue_character(char character){
    btstack_ring_buffer_write(&send_buffer, (uint8_t *) &character, 1);
    if (send_active == false) {
        send_next(&send_timer);
    }
}

// GPIO interrupt handler for buttons
void gpio_callback(uint gpio, uint32_t events) {
    // Only process on falling edge (button press)
    if (events & GPIO_IRQ_EDGE_FALL) {
        // Get current time for debouncing
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        uint8_t button_index;
        char key_to_send = 0;
        
        // Map GPIO to array index and key character
        switch(gpio) {
            case GPIO_BUTTON_D:
                button_index = 0;
                key_to_send = 'd';
                break;
            case GPIO_BUTTON_W:
                button_index = 1;
                key_to_send = 'w';
                break;
            case GPIO_BUTTON_A:
                button_index = 2;
                key_to_send = 'a';
                break;
            case GPIO_BUTTON_S:
                button_index = 3;
                key_to_send = 's';
                break;
            default:
                return; // Unknown GPIO
        }
        
        // Debounce check
        if (current_time - last_button_press_time[button_index] > DEBOUNCE_MS) {
            last_button_press_time[button_index] = current_time;
            
            if (app_state == APP_CONNECTED) {
                // Send key press if connected
                printf("Button press on GPIO %d - sending '%c'\n", gpio, key_to_send);
                queue_character(key_to_send);
            } else if (app_state == APP_NOT_CONNECTED && gpio == GPIO_BUTTON_D) {
                // Use button D to initiate connection if not connected
                printf("Button press on GPIO %d - connecting to %s\n", gpio, bd_addr_to_str(device_addr));
                hid_device_connect(device_addr, &hid_cid);
                app_state = APP_CONNECTING;
            }
        }
    }
}

// Initialize GPIO for status LED
static void init_status_led(void) {
    gpio_init(GPIO_STATUS_LED);
    gpio_set_dir(GPIO_STATUS_LED, GPIO_OUT);
    gpio_put(GPIO_STATUS_LED, 0);  // Start with LED OFF
    printf("Status LED initialized on GPIO %d\n", GPIO_STATUS_LED);
}

// Initialize GPIO for buttons
static void init_gpio_buttons(void) {
    // Initialize all buttons with pull-ups
    gpio_init(GPIO_BUTTON_D);
    gpio_set_dir(GPIO_BUTTON_D, GPIO_IN);
    gpio_pull_up(GPIO_BUTTON_D);
    
    gpio_init(GPIO_BUTTON_W);
    gpio_set_dir(GPIO_BUTTON_W, GPIO_IN);
    gpio_pull_up(GPIO_BUTTON_W);
    
    gpio_init(GPIO_BUTTON_A);
    gpio_set_dir(GPIO_BUTTON_A, GPIO_IN);
    gpio_pull_up(GPIO_BUTTON_A);
    
    gpio_init(GPIO_BUTTON_S);
    gpio_set_dir(GPIO_BUTTON_S, GPIO_IN);
    gpio_pull_up(GPIO_BUTTON_S);
    
    // Set up GPIO interrupts for all buttons (falling edge)
    gpio_set_irq_enabled_with_callback(GPIO_BUTTON_D, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled(GPIO_BUTTON_W, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(GPIO_BUTTON_A, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(GPIO_BUTTON_S, GPIO_IRQ_EDGE_FALL, true);
    
    printf("GPIO buttons initialized (D=%d, W=%d, A=%d, S=%d)\n", 
           GPIO_BUTTON_D, GPIO_BUTTON_W, GPIO_BUTTON_A, GPIO_BUTTON_S);
}

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * packet, uint16_t packet_size){
    UNUSED(channel);
    UNUSED(packet_size);
    uint8_t status;
    switch (packet_type){
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)){
                case BTSTACK_EVENT_STATE:
                    if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
                    app_state = APP_NOT_CONNECTED;
                    update_status_led();  // Update LED status
                    printf("BTstack ready. Press button D to connect or pair.\n");
                    break;

                case HCI_EVENT_USER_CONFIRMATION_REQUEST:
                    // ssp: inform about user confirmation request
                    log_info("SSP User Confirmation Request with numeric value '%06"PRIu32"'\n", hci_event_user_confirmation_request_get_numeric_value(packet));
                    log_info("SSP User Confirmation Auto accept\n");                   
                    break; 

                case HCI_EVENT_HID_META:
                    switch (hci_event_hid_meta_get_subevent_code(packet)){
                        case HID_SUBEVENT_CONNECTION_OPENED:
                            status = hid_subevent_connection_opened_get_status(packet);
                            if (status != ERROR_CODE_SUCCESS) {
                                // outgoing connection failed
                                printf("Connection failed, status 0x%x\n", status);
                                app_state = APP_NOT_CONNECTED;
                                hid_cid = 0;
                                update_status_led();  // Update LED status
                                return;
                            }
                            app_state = APP_CONNECTED;
                            hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
                            update_status_led();  // Update LED status
                            printf("HID Connected! Press WASD buttons to send keystrokes.\n");
                            break;
                        case HID_SUBEVENT_CONNECTION_CLOSED:
                            btstack_run_loop_remove_timer(&send_timer);
                            printf("HID Disconnected\n");
                            app_state = APP_NOT_CONNECTED;
                            hid_cid = 0;
                            update_status_led();  // Update LED status
                            break;
                        case HID_SUBEVENT_CAN_SEND_NOW:
                            if (send_keycode){
                                send_report(send_modifier, send_keycode);
                                // schedule key up
                                send_keycode = 0;
                                send_modifier = 0;
                                btstack_run_loop_set_timer_handler(&send_timer, trigger_key_up);
                                btstack_run_loop_set_timer(&send_timer, TYPING_KEYDOWN_MS);
                            } else {
                                send_report(0, 0);
                                // schedule next key down
                                btstack_run_loop_set_timer_handler(&send_timer, send_next);
                                btstack_run_loop_set_timer(&send_timer, TYPING_DELAY_MS);
                            }
                            btstack_run_loop_add_timer(&send_timer);
                            break;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

/* @section Main Application Setup
 *
 * @text Listing MainConfiguration shows main application code. 
 * To run a HID Device service you need to initialize the SDP, and to create and register HID Device record with it. 
 * At the end the Bluetooth stack is started.
 */

int btstack_main(int argc, const char * argv[]){
    (void)argc;
    (void)argv;

    // Initialize Pico stdio
    stdio_init_all();
    
    // Allow time for USB to initialize
    sleep_ms(2000);
    
    printf("\n\nHID Keyboard Demo with GPIO Buttons Starting...\n");
    
    // Initialize status LED
    init_status_led();
    
    // Initialize GPIO buttons
    init_gpio_buttons();
    
    // Parse the target Bluetooth address
    sscanf_bd_addr(device_addr_string, device_addr);

    // allow to get found by inquiry
    gap_discoverable_control(1);
    // use Limited Discoverable Mode; Peripheral; Keyboard as CoD
    gap_set_class_of_device(0x2540);
    // set local name to be identified - zeroes will be replaced by actual BD ADDR
    gap_set_local_name("HID Keyboard Demo 00:00:00:00:00:00");
    // allow for role switch in general and sniff mode
    gap_set_default_link_policy_settings( LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE );
    // allow for role switch on outgoing connections - this allow HID Host to become master when we re-connect to it
    gap_set_allow_role_switch(true);

    // L2CAP
    l2cap_init();

#ifdef ENABLE_BLE
    // Initialize LE Security Manager. Needed for cross-transport key derivation
    sm_init();
#endif

    // SDP Server
    sdp_init();
    memset(hid_service_buffer, 0, sizeof(hid_service_buffer));

    uint8_t hid_virtual_cable = 0;
    uint8_t hid_remote_wake = 1;
    uint8_t hid_reconnect_initiate = 1;
    uint8_t hid_normally_connectable = 1;

    hid_sdp_record_t hid_params = {
        // hid sevice subclass 2540 Keyboard, hid counntry code 33 US
        0x2540, 33, 
        hid_virtual_cable, hid_remote_wake, 
        hid_reconnect_initiate, hid_normally_connectable,
        hid_boot_device,
        host_max_latency, host_min_timeout,
        3200,
        hid_descriptor_keyboard,
        sizeof(hid_descriptor_keyboard),
        hid_device_name
    };
    
    hid_create_sdp_record(hid_service_buffer, sdp_create_service_record_handle(), &hid_params);
    btstack_assert(de_get_len( hid_service_buffer) <= sizeof(hid_service_buffer));
    sdp_register_service(hid_service_buffer);

    // See https://www.bluetooth.com/specifications/assigned-numbers/company-identifiers if you don't have a USB Vendor ID and need a Bluetooth Vendor ID
    // device info: BlueKitchen GmbH, product 1, version 1
    device_id_create_sdp_record(device_id_sdp_service_buffer, sdp_create_service_record_handle(), DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH, BLUETOOTH_COMPANY_ID_BLUEKITCHEN_GMBH, 1, 1);
    btstack_assert(de_get_len( device_id_sdp_service_buffer) <= sizeof(device_id_sdp_service_buffer));
    sdp_register_service(device_id_sdp_service_buffer);

    // HID Device
    hid_device_init(hid_boot_device, sizeof(hid_descriptor_keyboard), hid_descriptor_keyboard);
       
    // register for HCI events
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // register for HID events
    hid_device_register_packet_handler(&packet_handler);

    btstack_ring_buffer_init(&send_buffer, send_buffer_storage, sizeof(send_buffer_storage));

    // turn on!
    hci_power_control(HCI_POWER_ON);
    
    printf("Bluetooth stack initialized. Press button D to connect.\n");
    
    return 0;
}