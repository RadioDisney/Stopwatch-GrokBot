// Host check: compile with main/face_scaler.cpp, main/grokbot_player.cpp,
// zlib (-lz), and an esp_log.h shim on the include path.
#include "face_scaler.h"
#include "grokbot_player.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <vector>
#include <zlib.h>

int main(int argc, char **argv)
{
    constexpr int source = 400, output = 468;
    FaceScaler scaler;
    assert(scaler.init(source, output, output, 292.5f));
    std::vector<uint8_t> packed(source * source / 2), reference = packed;
    std::vector<uint16_t> screen(output * output), expected(screen.size());
    std::vector<uint16_t> strip(output * 16 + 2, 0x1234);
    FrameDamage force;
    force.all = true;
    for (int y = 0; y < output; y += 16) {
        int cursor = 0;
        const int rows = std::min(16, output - y);
        const FrameRegion full = scaler.next_dirty_region(force, y, rows, cursor);
        assert(full.x == 0 && full.y == y && full.width == output && full.height == rows);
        assert(scaler.next_dirty_region(force, y, rows, cursor).empty());
    }
    int checked = 0;
    uint64_t transmitted = 0;
    auto update = [&](const std::vector<uint8_t> &payload, bool delta) {
        for (size_t i = 0; i < reference.size(); ++i) {
            reference[i] = delta ? reference[i] ^ payload[i] : payload[i];
        }
        FrameDamage damage;
        update_packed_frame(packed.data(), payload.data(), source, source, delta, &damage);
        assert(packed == reference);
        for (int y = 0; y < output; y += 16) {
            const int rows = std::min(16, output - y);
            int next_x = 0;
            while (true) {
                const FrameRegion region = scaler.next_dirty_region(damage, y, rows, next_x);
                if (region.empty()) break;
                assert(region.x >= 0 && region.y >= 0 && region.x + region.width <= output);
                assert(region.y + region.height <= output && !(region.x & 1) && !(region.width & 1));
                scaler.render_region(packed.data(), strip.data() + 1, region);
                assert(strip.front() == 0x1234 && strip.back() == 0x1234);
                for (int row = 0; row < rows; ++row) {
                    std::copy_n(strip.data() + 1 + row * region.width, region.width,
                                screen.data() + (y + row) * output + region.x);
                }
                transmitted += static_cast<uint64_t>(region.width) * region.height;
            }
        }
        // Independent full redraw, compared against the retained partial screen.
        scaler.render(reference.data(), expected.data());
        assert(screen == expected);
        ++checked;
    };

    std::vector<uint8_t> payload(packed.size());
    update(payload, false);  // No changes: no transfer.
    // Single pixels at crop, atlas, and packed-byte boundaries.
    for (int y : {0, 52, 53, 54, 199, 200, 345, 346, 347, 399}) {
        for (int x : {0, 1, 7, 8, 52, 53, 54, 199, 200, 345, 346, 347, 398, 399}) {
            std::fill(payload.begin(), payload.end(), 0);
            const int pixel = y * source + x;
            payload[pixel / 2] = (pixel & 1) ? 0x0f : 0xf0;
            update(payload, true);
        }
    }
    std::mt19937 random(42);
    for (int i = 0; i < 100; ++i) {
        std::fill(payload.begin(), payload.end(), 0);
        const int x = random() % 190, y = random() % 390;
        for (int row = 0; row < 10; ++row) {
            for (int col = 0; col < 10; ++col) payload[(y + row) * 200 + x + col] = random();
        }
        update(payload, true);
    }
    // Dense absolute frames test complete clearing, and word loads must work unaligned.
    for (auto &byte : payload) byte = random();
    update(payload, false);
    std::vector<uint8_t> unaligned(packed.size() + 1);
    std::copy(packed.begin(), packed.end(), unaligned.begin() + 1);
    std::fill(payload.begin(), payload.end(), 0);
    update_packed_frame(unaligned.data() + 1, payload.data(), source, source, false);
    assert(std::all_of(unaligned.begin(), unaligned.end(), [](uint8_t b) { return b == 0; }));
    update(payload, false);

    if (argc == 2) {
        std::ifstream input(argv[1], std::ios::binary);
        assert(input.good());
        const std::vector<uint8_t> atlas((std::istreambuf_iterator<char>(input)), {});
        GrokBotPlayer player;
        assert(player.init(atlas.data(), atlas.size()));
        for (int clip = 0; clip < player.clip_count(); ++clip) {
            assert(player.select(clip));
            std::vector<uint8_t> cache;
            for (int index = 0; index < player.selected_frame_count(); ++index) {
                GrokBotPlayer::FrameInfo frame = {};
                assert(player.frame_info(index, &frame));
                uLongf bytes = payload.size();
                assert(uncompress(payload.data(), &bytes, frame.data, frame.compressed_size) == Z_OK);
                assert(bytes == payload.size());
                update(payload, frame.delta);
                if (index == player.start_frame()) cache = reference;
            }
            update(cache, false);  // Cached loop restart must erase the tail correctly.
        }
    }
    std::printf("PASS: %d full/partial frame comparisons, 16-row tile strips, crop edges, "
                "unchanged frames, state clearing and cached restarts; transferred %.1f%% of full frames\n",
                checked, 100.0 * transmitted / (static_cast<uint64_t>(checked) * output * output));
}
