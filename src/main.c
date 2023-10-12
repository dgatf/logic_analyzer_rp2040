/*
 * Logic Analyzer RP2040-SUMP
 * Copyright (C) 2023 Daniel Gorbea <danielgorbea@hotmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "common.h"
#include "capture.h"
#include "protocol_sump.h"
#include "string.h"

volatile bool send_samples_ = false;
bool is_capturing_;
char debug_message_[DEBUG_BUFFER_SIZE];
config_t config_;
capture_config_t capture_config_;

void capture(void);
void complete_handler(void);
void set_pin_config(void);

int main()
{
    // init
    if (clock_get_hz(clk_sys) != 100000000)
        set_sys_clock_khz(100000, true);
    stdio_init_all();
    set_pin_config();
    config_.channels = capture_config_.channels = CHANNEL_COUNT;

    // debug init
    debug_init(&debug_message_[0], &config_.debug);
    debug("\n\nRP2040 Logic Analyzer - v0.1");
    debug("\nConfiguration:"
          "\n-Override trigger edge: %s",
          config_.trigger_edge ? "enabled" : "disabled");

    // led blink
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(500);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);

    capture_init(0, capture_config_.channels, complete_handler);

    while (true)
    {
        command_t command = sump_read();
        if (command == COMMAND_CAPTURE)
        {
            gpio_put(PICO_DEFAULT_LED_PIN, 1);
            capture();
        }
        else if (command == COMMAND_RESET)
        {
            if (capture_is_busy())
                capture_abort();
            sump_reset();
            gpio_put(PICO_DEFAULT_LED_PIN, 0);
        }
        if (send_samples_)
        {
            sump_send_samples();
            gpio_put(PICO_DEFAULT_LED_PIN, 0);
            send_samples_ = false;
        }
    }
}

void capture(void)
{
    is_capturing_ = true;
    capture_start(capture_config_.total_samples, capture_config_.rate, capture_config_.pre_trigger_samples);
}

void complete_handler(void)
{
    is_capturing_ = false;
    send_samples_ = true;
    debug("\nCapture complete. Samples count: %u Pre trigger count: %u ", get_samples_count(), get_pre_trigger_count());
    if (get_pre_trigger_count() < capture_config_.pre_trigger_samples)
    {
        debug("\nWarning. Not enough pre trigger samples. Missing samples (%u) will be sent as 0x0000 samples", capture_config_.pre_trigger_samples - get_pre_trigger_count());
    }
}

void set_pin_config(void)
{
    /*
     *   Connect GPIO to GND at boot to select/enable:
     *   - GPIO 19: triggers based on stages. Otherwise all triggers are trigger edge
     *   - GPIO 18: debug mode on. Output is on GPIO 16 at 115200bps.
     *
     *   Defaults (option not grounded):
     *   - Override trigger edge enabled
     *   - Debug disabled
     */

    // configure pins
    gpio_init_mask((1 << GPIO_DEBUG_ENABLE) | (1 << GPIO_TRIGGER_STAGES));
    gpio_set_dir_masked((1 << GPIO_DEBUG_ENABLE) | (1 << GPIO_TRIGGER_STAGES), false);
    gpio_pull_up(GPIO_TRIGGER_STAGES);
    gpio_pull_up(GPIO_DEBUG_ENABLE);

    // set default config
    config_.trigger_edge = true;
    config_.debug = false;

    // read pin config
    if (!gpio_get(GPIO_TRIGGER_STAGES))
        config_.trigger_edge = false;
    if (!gpio_get(GPIO_DEBUG_ENABLE))
        config_.debug = true;
}
