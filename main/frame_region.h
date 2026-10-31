#pragma once

#include <stdint.h>
#include <string.h>

struct FrameRegion {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool empty() const { return width == 0 || height == 0; }
};

struct FrameDamage {
    static constexpr int kTileSize = 16;
    static constexpr int kMaxRows = 32;  // Covers source images up to 512x512.
    uint32_t rows[kMaxRows] = {};
    FrameRegion bounds;
    bool all = false;
};

// The 400px atlas has 50 groups of eight pixels per row. Compare/update one
// 32-bit group at a time and bound the changed source pixels conservatively.
// memcpy keeps these loads valid even when a host buffer is not word aligned.
inline FrameRegion update_packed_frame(uint8_t *packed, const uint8_t *payload,
                                       int width, int height, bool delta, FrameDamage *damage = nullptr)
{
    if (damage != nullptr) *damage = {};
    int left = width;
    int right = -1;
    int top = height;
    int bottom = -1;
    const int groups = width / 8;  // Caller requires a multiple-of-eight width.
    for (int y = 0; y < height; ++y) {
        for (int group = 0; group < groups; ++group) {
            const int offset = (y * groups + group) * 4;
            uint32_t previous, value;
            memcpy(&previous, packed + offset, sizeof(previous));
            memcpy(&value, payload + offset, sizeof(value));
            const uint32_t changed = delta ? value : (previous ^ value);
            if (changed == 0) continue;
            value = previous ^ changed;
            memcpy(packed + offset, &value, sizeof(value));
            if (damage != nullptr) {
                damage->rows[y / FrameDamage::kTileSize] |=
                    1U << (group * 8 / FrameDamage::kTileSize);
            }
            if (group * 8 < left) left = group * 8;
            if (group * 8 + 7 > right) right = group * 8 + 7;
            if (y < top) top = y;
            bottom = y;
        }
    }
    const FrameRegion bounds = right < left ? FrameRegion{} :
        FrameRegion{left, top, right - left + 1, bottom - top + 1};
    if (damage != nullptr) damage->bounds = bounds;
    return bounds;
}
