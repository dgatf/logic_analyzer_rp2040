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

#ifndef CAPTURE
#define CAPTURE

#ifdef __cplusplus
extern "C"
{
#endif

#include "hardware/irq.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "common.h"
#include "capture.pio.h"

    typedef void (*complete_handler_t)(void);

    extern capture_config_t capture_config_;
    extern config_t config_;

    void capture_init(uint pin_base, uint pin_count, complete_handler_t handler);
    void capture_start(uint samples, uint rate, uint pre_trigger_samples);
    void capture_abort(void);
    bool capture_is_busy(void);
    uint get_sample_index(int index);
    uint get_samples_count(void);
    uint get_pre_trigger_count(void);

#ifdef __cplusplus
}
#endif

#endif
