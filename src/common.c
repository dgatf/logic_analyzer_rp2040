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

char *buffer_;
bool *is_enabled_;

void debug_init(char *buffer, bool *is_enabled)
{
    buffer_ = buffer;
    is_enabled_ = is_enabled;
    if (is_enabled_)
    {
        uart_init(uart0, 115200);
        uart_set_fifo_enabled(uart0, true);
        gpio_set_function(16, GPIO_FUNC_UART);
    }
}

void debug_reinit(void)
{
    if (*is_enabled_)
    {
        uart_init(uart0, 115200);
        uart_set_fifo_enabled(uart0, true);
        gpio_set_function(16, GPIO_FUNC_UART);
    }
}

void debug(const char *format, ...)
{
    if (*is_enabled_)
    {
        va_list args;
        va_start(args, format);
        vsprintf(buffer_, format, args);
        uart_puts(uart0, buffer_);
        va_end(args);
    }
}

void debug_block(const char *format, ...)
{
    if (*is_enabled_)
    {
        va_list args;
        va_start(args, format);
        vsprintf(buffer_, format, args);
        uart_puts(uart0, buffer_);
        va_end(args);
        uart_tx_wait_blocking(uart0);
    }
}

bool debug_is_enabled(void)
{
    return *is_enabled_;
}
