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

#ifndef COMMON
#define COMMON

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <stdarg.h>
#include "hardware/uart.h"
#include "hardware/gpio.h"

// Number of channels
#define CHANNEL_COUNT 16

// Maximum number of triggers
#define TRIGGERS_COUNT 4

// Debug buffer size
#define DEBUG_BUFFER_SIZE 300

    typedef enum gpio_config_t
    {
        GPIO_DEBUG_ENABLE = 18,
        GPIO_TRIGGER_STAGES = 19  // If gpio 20 grounded: triggers are based on stages. If gpio 20 not grounded: all triggers at stage 0 are edge triggers 
    } gpio_config_t;

    typedef enum command_t
    {
        COMMAND_NONE,
        COMMAND_RESET,
        COMMAND_CAPTURE
    } command_t;

    typedef enum trigger_match_t
    {
        TRIGGER_TYPE_LEVEL_LOW,
        TRIGGER_TYPE_LEVEL_HIGH,
        TRIGGER_TYPE_EDGE_LOW,
        TRIGGER_TYPE_EDGE_HIGH
    } trigger_match_t;

    typedef struct trigger_t
    {
        bool is_enabled;
        uint pin;
        trigger_match_t match;
    } trigger_t;

    typedef struct config_t
    {
        uint channels;
        bool trigger_edge;
        bool debug;
    } config_t;

    typedef struct capture_config_t
    {
        uint total_samples;
        uint rate;
        uint pre_trigger_samples;
        uint channels;
        trigger_t trigger[4];
    } capture_config_t;

    void debug_init(uint baudrate, char *buffer, bool *is_enabled);
    void debug_reinit(void);
    void debug(const char *format, ...);
    void debug_block(const char *format, ...);
    bool debug_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif
