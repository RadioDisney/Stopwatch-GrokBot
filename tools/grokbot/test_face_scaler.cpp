// Host regression check: c++ -std=c++17 -O2 -I main \
//   tools/grokbot/test_face_scaler.cpp main/face_scaler.cpp -o /tmp/test_face_scaler
#include "face_scaler.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <vector>

static int unpack(const std::vector<uint8_t> &packed, int x, int y, int size)
{
    const int index = y * size + x;
    return (packed[index / 2] >> ((index & 1) ? 0 : 4)) & 15;
}

int main(int argc, char **argv)
{
    FaceScaler scaler;
    if (argc == 1) {
        assert(!scaler.init(400, 469, 468, 240));
        assert(!scaler.init(400, 468, 468, 401));
        assert(!scaler.init(1, 468, 468, 1));
        std::mt19937 random(42);
        for (const int source : {2, 17, 400}) {
            for (const int output : {1, 7, 240, 468}) {
                std::vector<float> crops = {1.0f, source / 2.0f, static_cast<float>(source)};
                if (source == 400) crops.push_back(292.5f);
                for (const float crop : crops) {
                    std::vector<uint8_t> packed((source * source + 1) / 2);
                    std::vector<uint16_t> pixels(output * output + 2, 0x1234);
                    for (const int pattern : {0, 1, 2}) {
                        for (auto &byte : packed) {
                            byte = pattern == 0 ? 0 : pattern == 1 ? 255 : random() & 255;
                        }
                        assert(scaler.init(source, output, output, crop));
                        scaler.render(packed.data(), pixels.data() + 1);
                        assert(pixels.front() == 0x1234 && pixels.back() == 0x1234);
                        for (int y = 0; y < output; ++y) {
                            for (int x = 0; x < output; ++x) {
                                // Independent floating point reference, without row caching.
                                const double sx = std::clamp((source - crop) / 2.0 +
                                    (x + 0.5) * crop / output - 0.5, 0.0, source - 1.0);
                                const double sy = std::clamp((source - crop) / 2.0 +
                                    (y + 0.5) * crop / output - 0.5, 0.0, source - 1.0);
                                const int x0 = sx, y0 = sy;
                                const int x1 = std::min(x0 + 1, source - 1);
                                const int y1 = std::min(y0 + 1, source - 1);
                                const double fx = sx - x0, fy = sy - y0;
                                const double top = unpack(packed, x0, y0, source) * (1 - fx) +
                                                   unpack(packed, x1, y0, source) * fx;
                                const double bottom = unpack(packed, x0, y1, source) * (1 - fx) +
                                                      unpack(packed, x1, y1, source) * fx;
                                const int gray = std::lround((top * (1 - fy) + bottom * fy) * 17);
                                const uint16_t stored = pixels[1 + y * output + x];
                                const uint16_t rgb = (stored >> 8) | (stored << 8);
                                assert(std::abs((rgb >> 11) - (gray >> 3)) <= 1);
                                assert(std::abs(((rgb >> 5) & 63) - (gray >> 2)) <= 1);
                                assert(std::abs((rgb & 31) - (gray >> 3)) <= 1);
                            }
                        }
                    }
                }
            }
        }
        std::puts("PASS: black, white and random frames; crop, interpolation, byte order and output bounds");
    }

    // Optional packed frame -> raw panel RGB565, for previews of the actual renderer.
    if (argc == 3) {
        std::vector<uint8_t> packed(400 * 400 / 2);
        std::vector<uint16_t> pixels(468 * 468);
        std::ifstream input(argv[1], std::ios::binary);
        assert(input.read(reinterpret_cast<char *>(packed.data()), packed.size()));
        assert(scaler.init(400, 468, 468, 292.5f));
        scaler.render(packed.data(), pixels.data());
        std::ofstream output(argv[2], std::ios::binary);
        output.write(reinterpret_cast<const char *>(pixels.data()), pixels.size() * sizeof(uint16_t));
        assert(output.good());
    }
}
