#include "GifWriter.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
    /* Independent GIF reader for the subset GifWriter emits: global 256-entry palette,
     * graphic control extensions, sub-rectangle frames composed onto the previous canvas. */
    struct DecodedGif {
        int width = 0, height = 0;
        std::vector<std::vector<uint8_t>> frames; // Full composed canvases.
        std::vector<int> delays;
        std::vector<int> rectangleAreas;
        bool loops = false;
    };

    int word(const std::vector<uint8_t> &data, size_t at) {
        return data[at] | (data[at + 1] << 8);
    }

    std::vector<uint8_t> readSubBlocks(const std::vector<uint8_t> &data, size_t &at) {
        std::vector<uint8_t> bytes;
        while (data[at]) {
            size_t size = data[at++];
            bytes.insert(bytes.end(), data.begin() + long(at), data.begin() + long(at + size));
            at += size;
        }
        ++at;
        return bytes;
    }

    std::vector<uint8_t> lzwDecode(const std::vector<uint8_t> &bytes, int minCodeSize,
                                   size_t pixelCount) {
        const int clear = 1 << minCodeSize, end = clear + 1;
        std::vector<std::vector<uint8_t>> table;
        auto reset = [&] {
            table.assign(size_t(end + 1), {});
            for (int i = 0; i < clear; ++i)
                table[size_t(i)] = {uint8_t(i)};
        };
        reset();
        int codeSize = minCodeSize + 1, previous = -1;
        size_t bit = 0;
        std::vector<uint8_t> out;
        while (bit + size_t(codeSize) <= bytes.size() * 8) {
            int code = 0;
            for (int i = 0; i < codeSize; ++i, ++bit)
                code |= ((bytes[bit / 8] >> (bit % 8)) & 1) << i;
            if (code == clear) {
                reset();
                codeSize = minCodeSize + 1;
                previous = -1;
                continue;
            }
            if (code == end)
                break;
            std::vector<uint8_t> entry;
            if (code < int(table.size()))
                entry = table[size_t(code)];
            else {
                assert(previous >= 0 && code == int(table.size()));
                entry = table[size_t(previous)];
                entry.push_back(table[size_t(previous)][0]);
            }
            out.insert(out.end(), entry.begin(), entry.end());
            if (previous >= 0 && table.size() < 4096) {
                auto added = table[size_t(previous)];
                added.push_back(entry[0]);
                table.push_back(added);
                if (table.size() == size_t(1 << codeSize) && codeSize < 12)
                    ++codeSize;
            }
            previous = code;
        }
        assert(out.size() == pixelCount);
        return out;
    }

    DecodedGif decode(const fs::path &path) {
        std::ifstream in(path, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        assert(data.size() > 13 && std::string(data.begin(), data.begin() + 6) == "GIF89a");
        DecodedGif gif;
        gif.width = word(data, 6);
        gif.height = word(data, 8);
        assert(data[10] == 0xf7);
        for (int level = 0; level < 256; ++level)
            for (int channel = 0; channel < 3; ++channel)
                assert(data[size_t(13 + level * 3 + channel)] == level);
        size_t at = 13 + 768;
        std::vector<uint8_t> canvas(size_t(gif.width * gif.height), 0);
        int delay = 0;
        while (data[at] != 0x3b) {
            if (data[at] == 0x21 && data[at + 1] == 0xff) {
                at += 2;
                size_t size = data[at++];
                gif.loops = std::string(data.begin() + long(at), data.begin() + long(at + size)) ==
                            "NETSCAPE2.0";
                at += size;
                readSubBlocks(data, at);
            } else if (data[at] == 0x21 && data[at + 1] == 0xf9) {
                assert(data[at + 2] == 4 && data[at + 3] == 4); // Disposal: leave in place.
                delay = word(data, at + 4);
                at += 8;
            } else {
                assert(data[at] == 0x2c);
                int left = word(data, at + 1), top = word(data, at + 3);
                int w = word(data, at + 5), h = word(data, at + 7);
                assert(data[at + 9] == 0 && left + w <= gif.width && top + h <= gif.height);
                at += 10;
                int minCodeSize = data[at++];
                auto pixels = lzwDecode(readSubBlocks(data, at), minCodeSize, size_t(w * h));
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                        canvas[size_t((top + y) * gif.width + left + x)] =
                            pixels[size_t(y * w + x)];
                gif.frames.push_back(canvas);
                gif.delays.push_back(delay);
                gif.rectangleAreas.push_back(w * h);
            }
        }
        return gif;
    }

    std::vector<uint8_t> pattern(int width, int height, int seed) {
        std::vector<uint8_t> frame(size_t(width * height));
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                frame[size_t(y * width + x)] = uint8_t(((x / 3) ^ (y / 2)) & 1 ? 255 : seed);
        return frame;
    }

    void checkRoundTripAndMerging(const fs::path &path) {
        const int width = 120, height = 70;
        auto first = pattern(width, height, 0);
        auto second = first;
        for (int y = 10; y < 20; ++y)
            for (int x = 40; x < 55; ++x)
                second[size_t(y * width + x)] = uint8_t(x * 7 + y);
        // Noisy frame: forces many LZW codes, dictionary resets and 12-bit codes.
        std::vector<uint8_t> noise(size_t(width * height));
        uint32_t state = 12345;
        for (auto &pixel : noise) {
            state = state * 1103515245 + 12345;
            pixel = uint8_t(state >> 24);
        }
        GifWriter writer;
        assert(writer.open(path, width, height));
        writer.addFrame(first, 100);
        writer.addFrame(first, 10); // Identical: merged into the first frame's delay.
        writer.addFrame(second, 10);
        writer.addFrame(noise, 17);
        writer.addFrame(first, 300);
        assert(writer.close());
        assert(writer.framesWritten() == 4);

        DecodedGif gif = decode(path);
        assert(gif.width == width && gif.height == height && gif.loops);
        assert(gif.frames.size() == 4);
        assert((gif.delays == std::vector<int>{110, 10, 17, 300}));
        assert(gif.frames[0] == first && gif.frames[1] == second);
        assert(gif.frames[2] == noise && gif.frames[3] == first);
        assert(gif.rectangleAreas[0] == width * height);
        assert(gif.rectangleAreas[1] == 15 * 10); // Only the changed rectangle is stored.
    }

    void checkUnchangedImageKeepsDelay(const fs::path &path) {
        auto frame = pattern(16, 8, 128);
        GifWriter writer;
        assert(writer.open(path, 16, 8));
        writer.addFrame(frame, 65000);
        writer.addFrame(frame, 1000); // Would overflow the 16-bit delay: emitted separately.
        assert(writer.close());
        DecodedGif gif = decode(path);
        assert(gif.frames.size() == 2 && gif.frames[1] == frame);
        assert((gif.delays == std::vector<int>{65000, 1000}));
    }
}

int main() {
    fs::path directory = fs::temp_directory_path() / "battlechess-gif-check";
    fs::create_directories(directory);
    checkRoundTripAndMerging(directory / "round-trip.gif");
    checkUnchangedImageKeepsDelay(directory / "long-delay.gif");
    fs::remove_all(directory);
    return 0;
}
