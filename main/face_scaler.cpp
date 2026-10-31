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

void FaceScaler::expand_row(const uint8_t *packed, int row, uint16_t *output) const
{
    const int offset = row * source_size_;
    for (int x = 0; x < width_; ++x) {
        const Sample &s = x_[x];
        const int p0 = offset + s.first;
        const int p1 = p0 + 1;
        const int a = (packed[p0 / 2] >> ((p0 & 1) ? 0 : 4)) & 15;
        const int b = (packed[p1 / 2] >> ((p1 & 1) ? 0 : 4)) & 15;
        output[x] = a * (256 - s.fraction) + b * s.fraction;
    }
}

void FaceScaler::render(const uint8_t *packed, uint16_t *pixels)
{
    int top_row = -1;
    int bottom_row = -1;
    uint16_t *top = rows_[0];
    uint16_t *bottom = rows_[1];
    for (int y = 0; y < height_; ++y) {
        const Sample &s = y_[y];
        if (bottom_row == s.first) {
            std::swap(top, bottom);
            std::swap(top_row, bottom_row);
        }
        if (top_row != s.first) {
            expand_row(packed, s.first, top);
            top_row = s.first;
        }
        if (bottom_row != s.first + 1) {
            expand_row(packed, s.first + 1, bottom);
            bottom_row = s.first + 1;
        }
        for (int x = 0; x < width_; ++x) {
            const uint32_t value = top[x] * (256 - s.fraction) + bottom[x] * s.fraction;
            *pixels++ = palette_[(value * 17 + 32768) >> 16];
        }
    }
}
