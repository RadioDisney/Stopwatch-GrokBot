#include "face_scaler.h"

#include <algorithm>

FaceScaler::Sample FaceScaler::sample(int position, int output_size, int source_size,
                                     float viewport_size)
{
    // Map pixel centers, keeping the crop centered on the source image.
    const int coordinate = std::clamp(
        static_cast<int>((2 * position + 1) * viewport_size * 128 / output_size +
            (source_size - viewport_size) * 128 - 128),
        0, (source_size - 1) * 256);
    return {static_cast<uint16_t>(std::min(coordinate / 256, source_size - 2)),
            static_cast<uint16_t>(coordinate == (source_size - 1) * 256
                                      ? 256 : coordinate % 256)};
}

bool FaceScaler::init(int source_size, int output_width, int output_height, float viewport_size)
{
    if (source_size < 2 || source_size > kMaxOutputSize || viewport_size < 1 ||
        viewport_size > source_size || output_width < 1 || output_height < 1 ||
        output_width > kMaxOutputSize || output_height > kMaxOutputSize) {
        return false;
    }
    source_size_ = source_size;
    width_ = output_width;
    height_ = output_height;
    for (int x = 0; x < width_; ++x) {
        x_[x] = sample(x, width_, source_size, viewport_size);
    }
    for (int y = 0; y < height_; ++y) {
        y_[y] = sample(y, height_, source_size, viewport_size);
    }
    for (int gray = 0; gray < 256; ++gray) {
        const uint16_t rgb565 = ((gray >> 3) << 11) | ((gray >> 2) << 5) | (gray >> 3);
        // M5Canvas stores RGB565 in panel byte order.
        palette_[gray] = static_cast<uint16_t>((rgb565 >> 8) | (rgb565 << 8));
    }
    return true;
}

void FaceScaler::expand_row(const uint8_t *packed, int row, uint16_t *output,
                            int start_x, int width) const
{
    const int offset = row * source_size_;
    for (int x = 0; x < width; ++x) {
        const Sample &s = x_[start_x + x];
        const int p0 = offset + s.first;
        const int p1 = p0 + 1;
        const int a = (packed[p0 / 2] >> ((p0 & 1) ? 0 : 4)) & 15;
        const int b = (packed[p1 / 2] >> ((p1 & 1) ? 0 : 4)) & 15;
        output[x] = a * (256 - s.fraction) + b * s.fraction;
    }
}

void FaceScaler::render(const uint8_t *packed, uint16_t *pixels)
{
    render_region(packed, pixels, {0, 0, width_, height_});
}

FrameRegion FaceScaler::map_region(FrameRegion source) const
{
    if (source.empty()) return {};
    int left = width_, right = -1, top = height_, bottom = -1;
    for (int x = 0; x < width_; ++x) {
        if (x_[x].first + 1 >= source.x && x_[x].first < source.x + source.width) {
            left = std::min(left, x);
            right = x;
        }
    }
    for (int y = 0; y < height_; ++y) {
        if (y_[y].first + 1 >= source.y && y_[y].first < source.y + source.height) {
            top = std::min(top, y);
            bottom = y;
        }
    }
    if (right < left || bottom < top) return {};
    // CO5300/M5GFX requires both x and width to be even.
    left &= ~1;
    right = std::min(width_ - 1, right | 1);
    return {left, top, right - left + 1, bottom - top + 1};
}

bool FaceScaler::needs_update(FrameRegion output, const FrameDamage &damage) const
{
    if (damage.all) return true;
    const int left = x_[output.x].first / FrameDamage::kTileSize;
    const int right = (x_[output.x + output.width - 1].first + 1) / FrameDamage::kTileSize;
    const int top = y_[output.y].first / FrameDamage::kTileSize;
    const int bottom = (y_[output.y + output.height - 1].first + 1) / FrameDamage::kTileSize;
    const uint32_t mask = ((1U << (right - left + 1)) - 1) << left;
    for (int row = top; row <= bottom; ++row) {
        if (damage.rows[row] & mask) return true;
    }
    return false;
}

FrameRegion FaceScaler::next_dirty_region(const FrameDamage &damage, int y, int rows,
                                          int &next_x) const
{
    int start = -1;
    while (next_x < width_) {
        const int width = std::min(16, width_ - next_x);
        if (needs_update({next_x, y, width, rows}, damage)) {
            if (start < 0) start = next_x;
        } else if (start >= 0) {
            break;
        }
        next_x += width;
    }
    return start < 0 ? FrameRegion{} : FrameRegion{start, y, next_x - start, rows};
}

void FaceScaler::render_region(const uint8_t *packed, uint16_t *pixels, FrameRegion region)
{
    int top_row = -1;
    int bottom_row = -1;
    uint16_t *top = rows_[0];
    uint16_t *bottom = rows_[1];
    for (int y = 0; y < region.height; ++y) {
        const Sample &s = y_[region.y + y];
        if (bottom_row == s.first) {
            std::swap(top, bottom);
            std::swap(top_row, bottom_row);
        }
        if (top_row != s.first) {
            expand_row(packed, s.first, top, region.x, region.width);
            top_row = s.first;
        }
        if (bottom_row != s.first + 1) {
            expand_row(packed, s.first + 1, bottom, region.x, region.width);
            bottom_row = s.first + 1;
        }
        for (int x = 0; x < region.width; ++x) {
            const uint32_t value = (top[x] << 8) + (static_cast<int>(bottom[x]) - top[x]) * s.fraction;
            *pixels++ = palette_[(value * 17 + 32768) >> 16];
        }
    }
}
