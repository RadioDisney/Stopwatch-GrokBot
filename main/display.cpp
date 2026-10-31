#include "display.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

M5StopWatch gfx;
M5Canvas canvas(&gfx);

bool display_init()
{
    if (!gfx.init()) {
        ESP_LOGE("display", "M5GFX init failed");
        return false;
    }
    // Draw straight into the CO5300 GRAM. The intermediate 468x468 PSRAM
    // framebuffer adds a full-frame copy and caps the animation near 24 FPS.
    gfx.disableFrameBuffer();
    gfx.fillScreen(TFT_BLACK);
    gfx.setBrightness(160);

    canvas.setPsram(true);
    canvas.setColorDepth(16);
    if (canvas.createSprite(gfx.width(), gfx.height()) == nullptr) {
        ESP_LOGE("display", "Canvas allocation failed (%d x %d)", gfx.width(), gfx.height());
        return false;
    }
    canvas.fillScreen(TFT_BLACK);
    ESP_LOGI("display", "M5GFX ready: gfx=%dx%d canvas=%dx%d buffer=%p", gfx.width(), gfx.height(),
             canvas.width(), canvas.height(), canvas.getBuffer());
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setTextDatum(textdatum_t::middle_center);
    return true;
}

int display_width()
{
    return canvas.width();
}

int display_height()
{
    return canvas.height();
}
