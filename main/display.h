#pragma once

#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_AMOLED.hpp>
#include "face_scaler.h"

class Panel_CO5300 : public lgfx::Panel_AMOLED {
public:
    Panel_CO5300()
    {
        _cfg.memory_width = _cfg.panel_width = 480;
        _cfg.memory_height = _cfg.panel_height = 480;
        _write_depth = lgfx::color_depth_t::rgb565_2Byte;
        _read_depth = lgfx::color_depth_t::rgb565_2Byte;
    }

    const uint8_t *getInitCommands(uint8_t listno) const override
    {
        static constexpr uint8_t list0[] = {
            0x11, 0 + CMD_INIT_DELAY,
            150,
            0xC4, 1,
            0x80,
            0x35, 1,
            0x80,
            0x44, 2,
            0x01, 0xD2,
            0x53, 1,
            0x20,
            0x20, 0,
            0x36, 1,
            0,
            0x51, 1,
            0xA0,
            0x29, 0,
            0xff, 0xff,
        };
        return listno == 0 ? list0 : nullptr;
    }

};

class M5StopWatch : public M5GFX {
    lgfx::Bus_SPI _bus_instance;
    Panel_CO5300 _panel_instance;

public:
    bool init_impl(bool use_reset, bool use_clear) override
    {
        auto bus_cfg = _bus_instance.config();
        bus_cfg.freq_write = 80000000;
        bus_cfg.freq_read = 10000000;
        bus_cfg.pin_sclk = GPIO_NUM_40;
        bus_cfg.pin_io0 = GPIO_NUM_41;
        bus_cfg.pin_io1 = GPIO_NUM_42;
        bus_cfg.pin_io2 = GPIO_NUM_46;
        bus_cfg.pin_io3 = GPIO_NUM_45;
        bus_cfg.spi_host = SPI2_HOST;
        bus_cfg.spi_mode = 0;
        bus_cfg.spi_3wire = true;
        bus_cfg.dma_channel = SPI_DMA_CH_AUTO;
        _bus_instance.config(bus_cfg);
        _panel_instance.setBus(&_bus_instance);

        auto panel_cfg = _panel_instance.config();
        panel_cfg.pin_rst = GPIO_NUM_NC;
        panel_cfg.pin_cs = GPIO_NUM_39;
        panel_cfg.panel_width = 468;
        panel_cfg.panel_height = 468;
        panel_cfg.offset_x = 6;
        panel_cfg.offset_y = 0;
        panel_cfg.readable = false;
        _panel_instance.config(panel_cfg);
        setPanel(&_panel_instance);

        lgfx::pinMode(GPIO_NUM_38, lgfx::pin_mode_t::input_pullup);
        if (!LGFX_Device::init_impl(use_reset, use_clear)) {
            return false;
        }
        enableFrameBuffer(true);
        _panel_instance.setBrightness(160);
        return true;
    }

    bool enableFrameBuffer(bool auto_display = false)
    {
        if (!_panel_instance.initPanelFb()) {
            return false;
        }
        auto *fb = _panel_instance.getPanelFb();
        if (fb == nullptr) {
            return false;
        }
        fb->setBus(&_bus_instance);
        fb->setAutoDisplay(auto_display);
        setPanel(fb);
        return true;
    }

    void disableFrameBuffer()
    {
        auto *fb = _panel_instance.getPanelFb();
        if (fb != nullptr) {
            _panel_instance.deinitPanelFb();
            setPanel(&_panel_instance);
        }
    }

    void setBrightness(uint8_t brightness)
    {
        _panel_instance.setBrightness(brightness);
    }
};

extern M5StopWatch gfx;

bool display_init();
int display_width();
int display_height();
void display_frame(const uint8_t *packed, FaceScaler &scaler, const FrameDamage &damage,
                   int64_t *scale_us, int64_t *push_us, uint64_t *updated_pixels);
