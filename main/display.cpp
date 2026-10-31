#include "display.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <algorithm>

M5StopWatch gfx;

namespace {
constexpr int kStripRows = 16;
uint16_t *s_strips[2] = {};
}

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

    // Two internal SRAM strips let the CPU scale the next strip while DMA
    // transmits the preceding one. Avoid a 438KB PSRAM sprite and its reread.
    for (auto &strip : s_strips) {
        strip = static_cast<uint16_t *>(heap_caps_malloc(
            gfx.width() * kStripRows * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        if (strip == nullptr) {
            for (auto *allocated : s_strips) free(allocated);
            ESP_LOGE("display", "DMA strip allocation failed");
            return false;
        }
    }
    ESP_LOGI("display", "M5GFX ready: %dx%d, two %d-row internal DMA strips",
             gfx.width(), gfx.height(), kStripRows);
    return true;
}

int display_width()
{
    return gfx.width();
}

int display_height()
{
    return gfx.height();
}

void display_frame(const uint8_t *packed, FaceScaler &scaler, const FrameDamage &damage,
                   int64_t *scale_us, int64_t *push_us, uint64_t *updated_pixels)
{
    if (!damage.all && damage.bounds.empty()) return;
    gfx.startWrite();
    int strip_index = 0;
    for (int y = 0; y < gfx.height(); y += kStripRows) {
        const int rows = std::min<int>(kStripRows, gfx.height() - y);
        int next_x = 0;
        while (true) {
            const FrameRegion region = scaler.next_dirty_region(damage, y, rows, next_x);
            if (region.empty()) break;
            uint16_t *pixels = s_strips[strip_index];
            const int64_t scale_start = esp_timer_get_time();
            scaler.render_region(packed, pixels, region);
            *scale_us += esp_timer_get_time() - scale_start;

            const int64_t push_start = esp_timer_get_time();
            gfx.waitDMA();
            gfx.pushImageDMA(region.x, region.y, region.width, region.height,
                             reinterpret_cast<const lgfx::swap565_t *>(pixels));
            *push_us += esp_timer_get_time() - push_start;
            *updated_pixels += static_cast<uint64_t>(region.width) * region.height;
            strip_index ^= 1;
        }
    }
    const int64_t push_start = esp_timer_get_time();
    gfx.waitDMA();
    gfx.endWrite();
    *push_us += esp_timer_get_time() - push_start;
}
