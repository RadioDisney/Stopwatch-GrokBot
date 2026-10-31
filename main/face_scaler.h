#pragma once

#include <stdint.h>
#include "frame_region.h"

// Expand a centered viewport of a 4-bit grayscale frame to the panel, using
// bilinear interpolation. Row caching avoids resampling duplicate source rows.
class FaceScaler {
public:
    static constexpr int kMaxOutputSize = 468;

    bool init(int source_size, int output_width, int output_height, float viewport_size);
    void render(const uint8_t *packed, uint16_t *pixels);
    FrameRegion map_region(FrameRegion source) const;
    bool needs_update(FrameRegion output, const FrameDamage &damage) const;
    FrameRegion next_dirty_region(const FrameDamage &damage, int y, int rows, int &next_x) const;
    void render_region(const uint8_t *packed, uint16_t *pixels, FrameRegion region);

private:
    struct Sample {
        uint16_t first;
        uint16_t fraction;
    };

    static Sample sample(int position, int output_size, int source_size, float viewport_size);
    void expand_row(const uint8_t *packed, int row, uint16_t *output, int x, int width) const;

    int source_size_ = 0;
    int width_ = 0;
    int height_ = 0;
    Sample x_[kMaxOutputSize] = {};
    Sample y_[kMaxOutputSize] = {};
    uint16_t rows_[2][kMaxOutputSize] = {};
    uint16_t palette_[256] = {};
};
