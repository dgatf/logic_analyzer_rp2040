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

#include "capture.h"
#include "string.h"

#define PRE_TRIGGER_RING_BITS 10
#define PRE_TRIGGER_BUFFER_SIZE (1 << PRE_TRIGGER_RING_BITS)
#define POST_TRIGGER_BUFFER_SIZE 100000
#define MAX_TRIGGER_COUNT 4
#define RATE_CHANGE_CLK 5000

static const uint sm_pre_trigger_ = 0,
                  sm_post_trigger_ = 1,
                  sm_mux_ = 3,
                  dma_channel_pre_trigger_ = 0,
                  dma_channel_post_trigger_ = 1,
                  dma_channel_pio0_ctrl_ = 2,
                  dma_channel_pio1_ctrl_ = 3,
                  sm_trigger_[MAX_TRIGGER_COUNT] = {0, 1, 2, 3};
static uint offset_pre_trigger_,
    offset_post_trigger_,
    pre_trigger_samples_,
    post_trigger_samples_,
    pre_trigger_count_,
    pin_count_,
    trigger_count_,
    sm_trigger_mask_,
    trigger_mask_,
    pin_base_,
    rate_,
    offset_mux_,
    offset_trigger_[MAX_TRIGGER_COUNT];
static int pre_trigger_first_;
static float clk_div_;
static volatile uint pio0_ctrl_ = (1 << sm_post_trigger_), pio1_ctrl_ = 0;
static uint16_t pre_trigger_buffer_[PRE_TRIGGER_BUFFER_SIZE] __attribute__((aligned(PRE_TRIGGER_BUFFER_SIZE * sizeof(uint16_t)))),
    post_trigger_buffer_[POST_TRIGGER_BUFFER_SIZE];
static bool is_capturing_ = false, is_aborting_ = false;
static pio_sm_config pio_config_trigger_[MAX_TRIGGER_COUNT],
    pio_config_pre_trigger_,
    pio_config_post_trigger_,
    pio_config_mux_;
static const uint src_[4] = {0, 1, 2, 3};

static void (*handler_)(void) = NULL;

static inline void capture_complete_handler(void);
static inline void trigger_handler(void);
static inline void capture_stop(void);
static inline bool set_trigger(trigger_t trigger);

void capture_init(uint pin_base, uint pin_count, complete_handler_t handler)
{
    handler_ = handler;
    pin_count_ = pin_count;
    pin_base_ = pin_base;
    if (pre_trigger_samples_ > PRE_TRIGGER_BUFFER_SIZE)
        pre_trigger_samples_ = PRE_TRIGGER_BUFFER_SIZE;
    if (post_trigger_samples_ > POST_TRIGGER_BUFFER_SIZE)
        post_trigger_samples_ = POST_TRIGGER_BUFFER_SIZE;

    // init pins
    for (uint i = 0; i < pin_count; i++)
    {
        gpio_set_dir(i, false);
        gpio_pull_down(i);
    }
}

void capture_start(uint samples, uint rate, uint pre_trigger_samples)
{
    pre_trigger_samples_ = pre_trigger_samples;
    post_trigger_samples_ = samples - pre_trigger_samples;
    rate_ = rate;

    if (pre_trigger_samples_ > PRE_TRIGGER_BUFFER_SIZE)
        pre_trigger_samples_ = PRE_TRIGGER_BUFFER_SIZE;
    if (post_trigger_samples_ > POST_TRIGGER_BUFFER_SIZE)
        post_trigger_samples_ = POST_TRIGGER_BUFFER_SIZE;

    // set sys clock
    if (rate > RATE_CHANGE_CLK)
    {
        if (clock_get_hz(clk_sys) != 200000000)
        {
            set_sys_clock_khz(200000, true);
            debug_reinit();
        }
        clk_div_ = (float)clock_get_hz(clk_sys) / rate;
    }
    else
    {
        if (clock_get_hz(clk_sys) != 100000000)
        {
            set_sys_clock_khz(100000, true);
            debug_reinit();
        }
        clk_div_ = (float)clock_get_hz(clk_sys) / rate / 32 / 10;
    }
    if (clk_div_ > 0xffff)
        clk_div_ = 0xffff;

    debug_block("\nSys Clk: %u Clk div (%s): %f", clock_get_hz(clk_sys), rate > RATE_CHANGE_CLK ? "fast" : "slow", clk_div_);

    // dma channel pio0 control: disable pre trigger and enable post trigger
    dma_channel_config config_dma_channel_pio0_ctrl = dma_channel_get_default_config(dma_channel_pio0_ctrl_);
    channel_config_set_transfer_data_size(&config_dma_channel_pio0_ctrl, DMA_SIZE_32);
    channel_config_set_write_increment(&config_dma_channel_pio0_ctrl, false);
    channel_config_set_read_increment(&config_dma_channel_pio0_ctrl, false);
    channel_config_set_dreq(&config_dma_channel_pio0_ctrl, pio_get_dreq(pio0, sm_mux_, false));
    channel_config_set_chain_to(&config_dma_channel_pio0_ctrl, dma_channel_pio1_ctrl_);
    dma_channel_configure(
        dma_channel_pio0_ctrl_,
        &config_dma_channel_pio0_ctrl,
        &pio0->ctrl, // write address
        &pio0_ctrl_, // read address
        1,
        false);

    // dma channel pio1 control: disable mux and triggers
    dma_channel_config config_dma_channel_pio1_ctrl = dma_channel_get_default_config(dma_channel_pio1_ctrl_);
    channel_config_set_transfer_data_size(&config_dma_channel_pio1_ctrl, DMA_SIZE_32);
    channel_config_set_write_increment(&config_dma_channel_pio1_ctrl, false);
    channel_config_set_read_increment(&config_dma_channel_pio1_ctrl, false);
    dma_channel_configure(
        dma_channel_pio1_ctrl_,
        &config_dma_channel_pio1_ctrl,
        &pio1->ctrl, // write address
        &pio1_ctrl_, // read address
        1,
        false);

    dma_channel_start(dma_channel_pio0_ctrl_);

    // pio mux
    offset_mux_ = pio_add_program(pio0, &mux_program);
    pio_config_mux_ = mux_program_get_default_config(offset_mux_);
    sm_config_set_clkdiv(&pio_config_mux_, 1);
    pio_set_irq0_source_enabled(pio0, (enum pio_interrupt_source)(pis_interrupt0), true);
    pio_sm_init(pio0, sm_mux_, offset_mux_, &pio_config_mux_);
    irq_set_exclusive_handler(PIO0_IRQ_0, trigger_handler);
    irq_set_enabled(PIO0_IRQ_0, true);

    // init pre trigger
    if (rate > RATE_CHANGE_CLK)
    {
        offset_pre_trigger_ = pio_add_program(pio0, &capture_program);
        pio_config_pre_trigger_ = capture_program_get_default_config(offset_pre_trigger_);
    }
    else
    {
        offset_pre_trigger_ = pio_add_program(pio0, &capture_slow_program);
        pio_config_pre_trigger_ = capture_slow_program_get_default_config(offset_pre_trigger_);
    }
    sm_config_set_in_pins(&pio_config_pre_trigger_, pin_base_);
    sm_config_set_in_shift(&pio_config_pre_trigger_, false, true, pin_count_);
    sm_config_set_clkdiv(&pio_config_pre_trigger_, clk_div_);
    pio_sm_init(pio0, sm_pre_trigger_, offset_pre_trigger_, &pio_config_pre_trigger_);
    pio_sm_restart(pio0, sm_pre_trigger_);
    if (rate > RATE_CHANGE_CLK)
        pio0->instr_mem[offset_pre_trigger_] = pio_encode_in(pio_pins, pin_count_);
    else
        pio0->instr_mem[offset_pre_trigger_] = pio_encode_in(pio_pins, pin_count_) | pio_encode_delay(31);
    dma_channel_config channel_config_pre_trigger = dma_channel_get_default_config(dma_channel_pre_trigger_);
    channel_config_set_transfer_data_size(&channel_config_pre_trigger, DMA_SIZE_16);
    channel_config_set_ring(&channel_config_pre_trigger, true, PRE_TRIGGER_RING_BITS);
    channel_config_set_write_increment(&channel_config_pre_trigger, true);
    channel_config_set_read_increment(&channel_config_pre_trigger, false);
    channel_config_set_dreq(&channel_config_pre_trigger, pio_get_dreq(pio0, sm_pre_trigger_, false));
    dma_channel_configure(
        dma_channel_pre_trigger_,
        &channel_config_pre_trigger,
        &pre_trigger_buffer_,        // write address
        &pio0->rxf[sm_pre_trigger_], // read address
        0xffffffff,
        true);

    // init post trigger
    if (rate > RATE_CHANGE_CLK)
    {
        offset_post_trigger_ = pio_add_program(pio0, &capture_program);
        pio_config_post_trigger_ = capture_program_get_default_config(offset_post_trigger_);
    }
    else
    {
        offset_post_trigger_ = pio_add_program(pio0, &capture_slow_program);
        pio_config_post_trigger_ = capture_slow_program_get_default_config(offset_post_trigger_);
    }
    sm_config_set_in_pins(&pio_config_post_trigger_, pin_base_);
    sm_config_set_in_shift(&pio_config_post_trigger_, false, true, pin_count_);
    sm_config_set_clkdiv(&pio_config_post_trigger_, clk_div_);
    pio_sm_init(pio0, sm_post_trigger_, offset_post_trigger_, &pio_config_post_trigger_);
    pio_sm_restart(pio0, sm_post_trigger_);
    if (rate > RATE_CHANGE_CLK)
        pio0->instr_mem[offset_post_trigger_] = pio_encode_in(pio_pins, pin_count_);
    else
        pio0->instr_mem[offset_post_trigger_] = pio_encode_in(pio_pins, pin_count_) | pio_encode_delay(31);
    dma_channel_config channel_config_post_trigger = dma_channel_get_default_config(dma_channel_post_trigger_);
    channel_config_set_transfer_data_size(&channel_config_post_trigger, DMA_SIZE_16);
    channel_config_set_write_increment(&channel_config_post_trigger, true);
    channel_config_set_read_increment(&channel_config_post_trigger, false);
    channel_config_set_dreq(&channel_config_post_trigger, pio_get_dreq(pio0, sm_post_trigger_, false));
    dma_channel_set_irq0_enabled(dma_channel_post_trigger_, true); // raise an interrupt when completed
    irq_set_exclusive_handler(DMA_IRQ_0, capture_complete_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    dma_channel_configure(
        dma_channel_post_trigger_,
        &channel_config_post_trigger,
        &post_trigger_buffer_,        // write address
        &pio0->rxf[sm_post_trigger_], // read address
        post_trigger_samples_,
        true);

    // init triggers
    trigger_count_ = 0;
    sm_trigger_mask_ = 0;
    uint i = 0;
    while (capture_config_.trigger[i].is_enabled)
    {
        set_trigger(capture_config_.trigger[i]);
        i++;
    }

    // start state machines
    if (!sm_trigger_mask_)
    {
        pio_sm_set_enabled(pio0, sm_post_trigger_, true);
    }
    else
    {
        pio_set_sm_mask_enabled(pio0, (1 << sm_pre_trigger_) | (1 << sm_mux_), true);
        pio_set_sm_mask_enabled(pio1, sm_trigger_mask_, true);
    }
    is_capturing_ = true;

    debug_block("\nCapture start. Samples: %u Rate: %u Pre trigger samples: %u", pre_trigger_samples_ + post_trigger_samples_, rate_, pre_trigger_samples_);
}

void capture_abort(void)
{

    if (clock_get_hz(clk_sys) != 100000000)
    {
        set_sys_clock_khz(100000, true);
        debug_reinit();
    }
    is_capturing_ = false;
    is_aborting_ = true;
    capture_stop();

    debug("\nCapture aborted");
}

bool capture_is_busy(void)
{
    return is_capturing_;
}

uint get_sample_index(int index)
{
    if (index < 0 || index > pre_trigger_count_ + post_trigger_samples_)
        return 0;
    if (index < pre_trigger_count_)
    {
        if (pre_trigger_first_ + index < 0)
            return pre_trigger_buffer_[PRE_TRIGGER_BUFFER_SIZE + pre_trigger_first_ + index];
        else if (pre_trigger_first_ + index > PRE_TRIGGER_BUFFER_SIZE)
            return pre_trigger_buffer_[pre_trigger_first_ + index - PRE_TRIGGER_BUFFER_SIZE];
        else
            return pre_trigger_buffer_[pre_trigger_first_ + index];
    }
    return post_trigger_buffer_[index - pre_trigger_count_];
}

uint get_samples_count(void)
{
    return pre_trigger_count_ + post_trigger_samples_;
}

uint get_pre_trigger_count(void)
{
    return pre_trigger_count_;
}

static inline void capture_complete_handler(void)
{
    if (clock_get_hz(clk_sys) != 100000000)
    {
        set_sys_clock_khz(100000, true);
        debug_reinit();
    }
    if (!is_aborting_)
    {
        capture_stop();

        // set pre trigger range
        pre_trigger_first_ = ((0xffffffff - dma_hw->ch[dma_channel_pre_trigger_].transfer_count) % PRE_TRIGGER_BUFFER_SIZE) - pre_trigger_samples_;
        pre_trigger_count_ = pre_trigger_samples_;
        if (pre_trigger_first_ < 0 && (0xffffffff - dma_hw->ch[dma_channel_pre_trigger_].transfer_count) < PRE_TRIGGER_BUFFER_SIZE)
        {
            pre_trigger_first_ = 0;
            pre_trigger_count_ = 0xffffffff - dma_hw->ch[dma_channel_pre_trigger_].transfer_count;
        }

        is_capturing_ = false;
        handler_();
    }
    else
    {
        is_aborting_ = false;
    }
    dma_hw->ints0 = 1u << dma_channel_post_trigger_;
}

static inline void trigger_handler(void)
{
    debug("\nTriggered channel %u", pio_sm_get_blocking(pio0, sm_mux_));
    pio_interrupt_clear(pio0, 0);
}

static inline void capture_stop(void)
{
    pio_set_sm_mask_enabled(pio0, (1 << sm_mux_) | (1 << sm_pre_trigger_) | (1 << sm_post_trigger_), false);
    pio_set_sm_mask_enabled(pio1, sm_trigger_mask_, false);
    dma_channel_abort(dma_channel_pre_trigger_);
    dma_channel_abort(dma_channel_post_trigger_);
    dma_channel_abort(dma_channel_pio0_ctrl_);
    dma_channel_abort(dma_channel_pio1_ctrl_);
    for (uint i = 0; i < trigger_count_; i++)
    {
        dma_channel_abort(4 + i);
        pio_sm_clear_fifos(pio1, sm_trigger_[i]);
    }
    pio_sm_clear_fifos(pio0, sm_mux_);
    pio_sm_clear_fifos(pio0, sm_pre_trigger_);
    pio_sm_clear_fifos(pio0, sm_post_trigger_);
    pio_clear_instruction_memory(pio0);
    pio_clear_instruction_memory(pio1);
}

static bool set_trigger(trigger_t trigger)
{
    uint dma_channel_trigger = trigger_count_ + 4;
    if (trigger_count_ < MAX_TRIGGER_COUNT)
    {
        switch (trigger.match)
        {
        case TRIGGER_TYPE_LEVEL_HIGH:
            offset_trigger_[trigger_count_] = pio_add_program(pio1, &trigger_level_high_program);
            pio_config_trigger_[trigger_count_] = trigger_level_high_program_get_default_config(offset_trigger_[trigger_count_]);
            break;
        case TRIGGER_TYPE_LEVEL_LOW:
            offset_trigger_[trigger_count_] = pio_add_program(pio1, &trigger_level_low_program);
            pio_config_trigger_[trigger_count_] = trigger_level_low_program_get_default_config(offset_trigger_[trigger_count_]);
            break;
        case TRIGGER_TYPE_EDGE_HIGH:
            offset_trigger_[trigger_count_] = pio_add_program(pio1, &trigger_edge_high_program);
            pio_config_trigger_[trigger_count_] = trigger_edge_high_program_get_default_config(offset_trigger_[trigger_count_]);
            break;
        case TRIGGER_TYPE_EDGE_LOW:
            offset_trigger_[trigger_count_] = pio_add_program(pio1, &trigger_edge_low_program);
            pio_config_trigger_[trigger_count_] = trigger_edge_high_program_get_default_config(offset_trigger_[trigger_count_]);
            break;
        }
        sm_config_set_clkdiv(&pio_config_trigger_[trigger_count_], clk_div_);
        sm_config_set_in_pins(&pio_config_trigger_[trigger_count_], trigger.pin);
        pio_sm_init(pio1, sm_trigger_[trigger_count_], offset_trigger_[trigger_count_], &pio_config_trigger_[trigger_count_]);
        sm_trigger_mask_ |= 1 << trigger_count_;

        dma_channel_config channel_config_trigger = dma_channel_get_default_config(dma_channel_trigger);
        channel_config_set_transfer_data_size(&channel_config_trigger, DMA_SIZE_32);
        channel_config_set_write_increment(&channel_config_trigger, false);
        channel_config_set_read_increment(&channel_config_trigger, false);
        channel_config_set_dreq(&channel_config_trigger, pio_get_dreq(pio1, sm_trigger_[trigger_count_], false));
        dma_channel_configure(
            dma_channel_trigger,
            &channel_config_trigger,
            &pio0->txf[sm_mux_],   // write address
            &src_[trigger_count_], // read address
            1,
            true);

        if (debug_is_enabled())
        {
            char match[15] = "";
            switch (capture_config_.trigger[trigger_count_].match)
            {
            case TRIGGER_TYPE_LEVEL_HIGH:
                strcpy(match, "Level High");
                break;
            case TRIGGER_TYPE_LEVEL_LOW:
                strcpy(match, "Level Low");
                break;
            case TRIGGER_TYPE_EDGE_HIGH:
                strcpy(match, "Edge High");
                break;
            case TRIGGER_TYPE_EDGE_LOW:
                strcpy(match, "Edge Low");
                break;
            }
            debug_block("\n-Set trigger %u Pin: %u Match: %s %s",
                        trigger_count_,
                        capture_config_.trigger[trigger_count_].pin,
                        match,
                        config_.trigger_edge ? "(override)" : "");
        }

        trigger_count_++;
        return true;
    }
    return false;
}
