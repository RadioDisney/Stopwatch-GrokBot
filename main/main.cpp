#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "board.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "miniz.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "grokbot_player.h"
#include "face_scaler.h"

extern "C" const uint8_t grokbot_atlas_start[] asm("_binary_grokbot_atlas_start");
extern "C" const uint8_t grokbot_atlas_end[] asm("_binary_grokbot_atlas_end");

namespace {
constexpr const char *TAG = "grokbot";
constexpr gpio_num_t kButtonA = GPIO_NUM_2;
constexpr gpio_num_t kButtonB = GPIO_NUM_1;
constexpr uint32_t kDebounceMs = 180;
constexpr int kAtlasSize = 400;
// A 292.5px viewport maps to the 468px panel at exactly 1.6x the original size.
// The neutral 240px face therefore becomes 384px wide.
constexpr float kFaceViewportSize = 292.5f;

GrokBotPlayer g_player;
FaceScaler g_face_scaler;
uint8_t *g_packed = nullptr;
uint8_t *g_delta = nullptr;
uint8_t *g_loop_start = nullptr;
bool g_loop_start_valid = false;
FrameDamage g_damage;
bool g_rebuild_pending = false;
bool g_button_a_last = true;
bool g_button_b_last = true;
bool g_touch_last = false;
uint16_t g_touch_start_x = 0;
uint16_t g_touch_start_y = 0;
uint16_t g_touch_last_x = 0;
uint16_t g_touch_last_y = 0;
int64_t g_touch_start_ms = 0;
int64_t g_button_a_edge = 0;
int64_t g_button_b_edge = 0;
int64_t g_decode_us = 0;
int64_t g_scale_us = 0;
int64_t g_push_us = 0;
int64_t g_input_us = 0;
int64_t g_max_work_us = 0;
uint64_t g_updated_pixels = 0;
int g_manual_index = 0;
const char *g_state_name = nullptr;

bool decode_frame(const GrokBotPlayer::FrameInfo &frame)
{
    if (frame.data == nullptr || frame.compressed_size == 0 ||
        g_packed == nullptr || g_delta == nullptr) {
        return false;
    }

    const size_t packed_size = g_player.packed_frame_size();
    const size_t unpacked = tinfl_decompress_mem_to_mem(
        g_delta, packed_size, frame.data, frame.compressed_size,
        TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (unpacked != packed_size) {
        ESP_LOGE(TAG, "inflate failed: %u/%u", static_cast<unsigned>(unpacked),
                 static_cast<unsigned>(packed_size));
        return false;
    }

    update_packed_frame(g_packed, g_delta, kAtlasSize, kAtlasSize, frame.delta, &g_damage);
    return true;
}

bool display_decoded_frame()
{
    display_frame(g_packed, g_face_scaler, g_damage, &g_scale_us, &g_push_us, &g_updated_pixels);
    return true;
}

bool render_current_frame()
{
    GrokBotPlayer::FrameInfo frame = {};
    if (!g_player.current_frame(&frame)) {
        return false;
    }
    const int64_t decode_start = esp_timer_get_time();
    if (!decode_frame(frame)) {
        ESP_LOGE(TAG, "Atlas frame decode failed");
        return false;
    }
    g_decode_us += esp_timer_get_time() - decode_start;
    return display_decoded_frame();
}

bool render_rebuilt_current()
{
    const int64_t decode_start = esp_timer_get_time();
    const int target = g_player.current_index();
    if (target == g_player.start_frame() && g_loop_start_valid) {
        // Restore the already reconstructed loop start without inflating frames 0/1/2.
        update_packed_frame(g_packed, g_loop_start, kAtlasSize, kAtlasSize, false, &g_damage);
    } else {
        for (int i = 0; i <= target; ++i) {
            GrokBotPlayer::FrameInfo frame = {};
            if (!g_player.frame_info(i, &frame) || !decode_frame(frame)) return false;
        }
        // Rebuilding traverses intermediate frames; refresh the final image in full.
        g_damage.all = true;
        if (target == g_player.start_frame() && g_loop_start != nullptr) {
            memcpy(g_loop_start, g_packed, g_player.packed_frame_size());
            g_loop_start_valid = true;
        }
    }
    g_decode_us += esp_timer_get_time() - decode_start;
    g_rebuild_pending = false;
    return display_decoded_frame();
}

void apply_state(int index)
{
    if (index < 0 || index >= g_player.clip_count()) {
        return;
    }
    g_manual_index = index;
    g_loop_start_valid = false;
    if (!g_player.select(index)) {
        return;
    }
    g_player.seek(g_player.start_frame());
    g_rebuild_pending = true;
    g_state_name = g_player.clip_name(index);
    ESP_LOGI(TAG, "state -> %s", g_state_name);
}

void switch_state(int delta)
{
    const int count = g_player.clip_count();
    const int next = (g_manual_index + delta + count) % count;
    apply_state(next);
}

void poll_inputs()
{
    const int64_t now_ms = esp_timer_get_time() / 1000;
    const bool a_down = gpio_get_level(kButtonA) == 0;
    const bool b_down = gpio_get_level(kButtonB) == 0;

    if (a_down && !g_button_a_last && now_ms - g_button_a_edge >= kDebounceMs) {
        g_button_a_edge = now_ms;
        switch_state(1);
    }
    if (b_down && !g_button_b_last && now_ms - g_button_b_edge >= kDebounceMs) {
        g_button_b_edge = now_ms;
        switch_state(-1);
    }
    g_button_a_last = a_down;
    g_button_b_last = b_down;

    uint16_t tx = 0;
    uint16_t ty = 0;
    const bool touching = board_touch_read(&tx, &ty);
    if (touching && !g_touch_last) {
        g_touch_start_x = g_touch_last_x = tx;
        g_touch_start_y = g_touch_last_y = ty;
        g_touch_start_ms = now_ms;
    } else if (touching) {
        g_touch_last_x = tx;
        g_touch_last_y = ty;
    } else if (!touching && g_touch_last) {
        const int dx = static_cast<int>(g_touch_last_x) - static_cast<int>(g_touch_start_x);
        const int dy = static_cast<int>(g_touch_last_y) - static_cast<int>(g_touch_start_y);
        if (now_ms - g_touch_start_ms < 500 && abs(dx) < 30 && abs(dy) < 30) {
            switch_state(1);
        }
    }
    g_touch_last = touching;
}

}  // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "StopWatch GrokBot starting");
    (void)board_init();
    if (!display_init()) {
        ESP_LOGE(TAG, "Display initialization failed");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    constexpr size_t kPackedFrameBytes = kAtlasSize * kAtlasSize / 2;
    g_packed = static_cast<uint8_t *>(heap_caps_malloc(
        kPackedFrameBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    g_delta = static_cast<uint8_t *>(heap_caps_malloc(
        kPackedFrameBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (g_packed == nullptr || g_delta == nullptr) {
        free(g_packed);
        free(g_delta);
        g_packed = static_cast<uint8_t *>(heap_caps_malloc(
            kPackedFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        g_delta = static_cast<uint8_t *>(heap_caps_malloc(
            kPackedFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (g_packed == nullptr || g_delta == nullptr) {
        ESP_LOGE(TAG, "4-bit frame buffers allocation failed");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    memset(g_packed, 0, kPackedFrameBytes);
    // This optional cache needs only 80KB in PSRAM, not a full RGB frame.
    g_loop_start = static_cast<uint8_t *>(heap_caps_malloc(
        kPackedFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Decode buffers: packed=%p delta=%p loop_cache=%p", g_packed, g_delta, g_loop_start);

    if (!g_player.init(grokbot_atlas_start, static_cast<size_t>(grokbot_atlas_end - grokbot_atlas_start))) {
        ESP_LOGE(TAG, "Animation atlas initialization failed");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    if (g_player.width() != kAtlasSize || g_player.height() != kAtlasSize ||
        g_player.packed_frame_size() != kPackedFrameBytes ||
        !g_face_scaler.init(kAtlasSize, display_width(), display_height(), kFaceViewportSize)) {
        ESP_LOGE(TAG, "Unsupported atlas or fullscreen canvas dimensions");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    ESP_LOGI(TAG, "Fullscreen face: %.1fx%.1f crop -> %dx%d canvas", kFaceViewportSize,
             kFaceViewportSize, display_width(), display_height());

    g_player.seek(g_player.start_frame());
    g_rebuild_pending = true;
    g_state_name = g_player.clip_name(0);
    ESP_LOGI(TAG, "Atlas: %dx%d, %d states, %u fps",
             g_player.width(), g_player.height(), g_player.clip_count(),
             static_cast<unsigned>(g_player.fps()));
    ESP_LOGI(TAG, "GROKBOT_READY states=%d fps=%u",
             g_player.clip_count(), static_cast<unsigned>(g_player.fps()));

    const int64_t frame_us = 1000000LL / g_player.fps();
    int64_t next_frame = esp_timer_get_time();
    uint32_t frames_since_report = 0;
    int64_t report_time = next_frame;

    while (true) {
        const int64_t work_start = esp_timer_get_time();
        poll_inputs();
        g_input_us += esp_timer_get_time() - work_start;

        const bool rendered = g_rebuild_pending ? render_rebuilt_current() : render_current_frame();
        if (rendered) {
            // advance() wraps to the clip's start frame. Rebuild its XOR base
            // from frame 0 before displaying it, never from the previous loop's tail.
            g_rebuild_pending = g_player.advance();
            ++frames_since_report;
        } else {
            g_rebuild_pending = true;
        }
        g_max_work_us = std::max(g_max_work_us, esp_timer_get_time() - work_start);

        next_frame += frame_us;
        const int64_t now = esp_timer_get_time();
        if (next_frame > now) {
            const int64_t wait_us = next_frame - now;
            if (wait_us >= 1000) {
                vTaskDelay(pdMS_TO_TICKS((wait_us + 500) / 1000));
            } else {
                esp_rom_delay_us(static_cast<uint32_t>(wait_us));
            }
        } else if (now - next_frame > frame_us) {
            next_frame = now;
        }

        const int64_t report_now = esp_timer_get_time();
        if (report_now - report_time >= 1000000) {
            const uint32_t frames = frames_since_report;
            const uint32_t divisor = frames ? frames : 1;
            const double fps = frames * 1000000.0 / (report_now - report_time);
            const double update_percent = 100.0 * g_updated_pixels /
                (static_cast<uint64_t>(divisor) * display_width() * display_height());
            ESP_LOGI(TAG, "fps=%.1f state=%s frame=%d/%d decode=%ldus scale=%ldus "
                         "push=%ldus input=%ldus max_work=%ldus update=%.1f%%",
                     fps, g_player.selected_name(), g_player.current_index(),
                     g_player.selected_frame_count(), static_cast<long>(g_decode_us / divisor),
                     static_cast<long>(g_scale_us / divisor), static_cast<long>(g_push_us / divisor),
                     static_cast<long>(g_input_us / divisor), static_cast<long>(g_max_work_us),
                     update_percent);
            frames_since_report = 0;
            g_decode_us = g_scale_us = g_push_us = g_input_us = g_max_work_us = 0;
            g_updated_pixels = 0;
            report_time = report_now;
        }
    }
}
