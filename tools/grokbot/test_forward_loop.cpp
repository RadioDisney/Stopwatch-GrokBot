// Host check with zlib and an esp_log.h shim on the include path.
#include "grokbot_player.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include <zlib.h>

static void decode(GrokBotPlayer &player, int index, std::vector<uint8_t> &pixels)
{
    GrokBotPlayer::FrameInfo frame = {};
    assert(player.frame_info(index, &frame));
    std::vector<uint8_t> unpacked(pixels.size());
    uLongf size = unpacked.size();
    assert(uncompress(unpacked.data(), &size, frame.data, frame.compressed_size) == Z_OK);
    assert(size == pixels.size());
    for (size_t i = 0; i < size; ++i) {
        pixels[i] = frame.delta ? pixels[i] ^ unpacked[i] : unpacked[i];
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    assert(input.good());
    const std::vector<uint8_t> atlas((std::istreambuf_iterator<char>(input)), {});
    GrokBotPlayer player;
    assert(player.init(atlas.data(), atlas.size()));
    std::vector<uint8_t> pixels;
    int displayed = 0;
    for (int clip = 0; clip < player.clip_count(); ++clip) {
        assert(player.select(clip));
        const int count = player.selected_frame_count();
        const int start = player.start_frame();
        // Independent reference: decode each clip once, including hidden frames 0/1.
        std::vector<std::vector<uint8_t>> reference;
        std::vector<uint8_t> frame(player.packed_frame_size());
        for (int index = 0; index < count; ++index) {
            decode(player, index, frame);
            reference.push_back(frame);
        }
        // Keep the preceding state's tail as dirty input to exercise state switching.
        pixels.resize(player.packed_frame_size(), 0xa5);
        assert(player.seek(start));
        bool rebuild = true;
        for (int step = 0; step < 3 * (count - start); ++step) {
            const int expected = start + step % (count - start);
            assert(player.current_index() == expected);
            if (rebuild) {
                for (int index = 0; index <= player.current_index(); ++index) {
                    decode(player, index, pixels);
                }
            } else {
                decode(player, player.current_index(), pixels);
            }
            assert(pixels == reference[expected]);
            rebuild = player.advance();
            assert(rebuild == (expected == count - 1));
            ++displayed;
        }
        assert(player.current_index() == start && rebuild);
    }
    std::printf("PASS: %d states, three forward loops each, %d displayed frames; "
                "frame order, XOR rebuilds and state switches verified\n",
                player.clip_count(), displayed);
}
