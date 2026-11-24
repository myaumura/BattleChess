#include "GifWriter.h"
#include <algorithm>
#include <array>

namespace
{
    constexpr int kMinCodeSize = 8;
    constexpr int kClearCode = 1 << kMinCodeSize, kEndCode = kClearCode + 1;
    constexpr int kMaxCodes = 4096;
    constexpr int kHashSize = 5003; // Prime larger than kMaxCodes, as in classic LZW compressors.
    // GIF delays are 16-bit hundredths of a second.
    constexpr int kMaxDelay = 65535;

    void putWord(std::ofstream& out, int value)
    {
        out.put(char(value & 0xff));
        out.put(char((value >> 8) & 0xff));
    }

    /* Packs variable-width LZW codes least-significant bit first into 255-byte sub-blocks. */
    class CodePacker
    {
    public:
        explicit CodePacker(std::ofstream& out) : out(out)
        {
        }

        void put(int code, int bits)
        {
            accumulator |= uint32_t(code) << used;
            used += bits;
            while (used >= 8)
            {
                byte(uint8_t(accumulator & 0xff));
                accumulator >>= 8;
                used -= 8;
            }
        }

        void finish()
        {
            if (used)
                byte(uint8_t(accumulator & 0xff));
            flushBlock();
            out.put(0); // Block terminator.
        }

    private:
        std::ofstream& out;
        uint32_t accumulator = 0;
        int used = 0;
        std::array<char, 255> block{};
        int blockSize = 0;

        void byte(uint8_t value)
        {
            block[size_t(blockSize++)] = char(value);
            if (blockSize == 255)
                flushBlock();
        }

        void flushBlock()
        {
            if (!blockSize)
                return;
            out.put(char(blockSize));
            out.write(block.data(), blockSize);
            blockSize = 0;
        }
    };

    void compress(std::ofstream& out, const std::vector<uint8_t>& indices)
    {
        out.put(char(kMinCodeSize));
        CodePacker packer(out);
        std::vector<int32_t> keys(kHashSize, -1);
        std::vector<uint16_t> codes(kHashSize);
        int codeSize = kMinCodeSize + 1, nextCode = kEndCode + 1;
        packer.put(kClearCode, codeSize);
        if (indices.empty())
        {
            packer.put(kEndCode, codeSize);
            packer.finish();
            return;
        }
        int prefix = indices[0];
        for (size_t i = 1; i < indices.size(); ++i)
        {
            int symbol = indices[i];
            int32_t key = (prefix << 8) | symbol;
            int slot = int((uint32_t(symbol) << 4 ^ uint32_t(prefix)) % kHashSize);
            int step = slot ? kHashSize - slot : 1;
            while (keys[size_t(slot)] >= 0 && keys[size_t(slot)] != key)
                slot = (slot - step + kHashSize) % kHashSize;
            if (keys[size_t(slot)] == key)
            {
                prefix = codes[size_t(slot)];
                continue;
            }
            packer.put(prefix, codeSize);
            if (nextCode < kMaxCodes)
            {
                keys[size_t(slot)] = key;
                codes[size_t(slot)] = uint16_t(nextCode);
                // Decoders widen codes once the next code no longer fits in the current size.
                if (nextCode++ == (1 << codeSize) && codeSize < 12)
                    ++codeSize;
            }
            else
            {
                packer.put(kClearCode, codeSize);
                std::fill(keys.begin(), keys.end(), -1);
                codeSize = kMinCodeSize + 1;
                nextCode = kEndCode + 1;
            }
            prefix = symbol;
        }
        packer.put(prefix, codeSize);
        packer.put(kEndCode, codeSize);
        packer.finish();
    }
}

bool GifWriter::open(const std::filesystem::path& path, int frameWidth, int frameHeight)
{
    width = frameWidth;
    height = frameHeight;
    written = 0;
    hasPending = false;
    previous.clear();
    out.open(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write("GIF89a", 6);
    putWord(out, width);
    putWord(out, height);
    out.put(char(0xf7)); // Global color table of 256 entries, 8 bits per channel.
    out.put(0); // Background color index.
    out.put(0); // Square pixels.
    for (int level = 0; level < 256; ++level)
        for (int channel = 0; channel < 3; ++channel)
            out.put(char(level));
    // NETSCAPE2.0 application extension: loop forever.
    out.put(char(0x21));
    out.put(char(0xff));
    out.put(11);
    out.write("NETSCAPE2.0", 11);
    out.put(3);
    out.put(1);
    putWord(out, 0);
    out.put(0);
    return bool(out);
}

void GifWriter::addFrame(const std::vector<uint8_t>& gray, int delayCentiseconds)
{
    if (hasPending&& gray == pending&& pendingDelay
    +delayCentiseconds <= kMaxDelay)
    {
        pendingDelay += delayCentiseconds;
        return;
    }
    if (hasPending)
        writeFrame(pending, pendingDelay);
    pending = gray;
    pendingDelay = delayCentiseconds;
    hasPending = true;
}

void GifWriter::writeFrame(const std::vector<uint8_t>& frame, int delayCentiseconds)
{
    int left = 0, top = 0, right = width, bottom = height;
    if (!previous.empty())
    {
        left = width;
        top = height;
        right = bottom = 0;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                if (frame[size_t(y * width + x)] != previous[size_t(y * width + x)])
                {
                    left = std::min(left, x);
                    right = std::max(right, x + 1);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y + 1);
                }
        if (right <= left)
        {
            // Unchanged image with a different delay: keep one pixel.
            left = top = 0;
            right = bottom = 1;
        }
    }
    // Graphic control extension: leave the frame in place for the next one, no transparency.
    out.put(char(0x21));
    out.put(char(0xf9));
    out.put(4);
    out.put(char(1 << 2));
    putWord(out, std::clamp(delayCentiseconds, 0, kMaxDelay));
    out.put(0);
    out.put(0);
    // Image descriptor for the changed rectangle.
    out.put(char(0x2c));
    putWord(out, left);
    putWord(out, top);
    putWord(out, right - left);
    putWord(out, bottom - top);
    out.put(0);
    std::vector<uint8_t> region;
    region.reserve(size_t((right - left) * (bottom - top)));
    for (int y = top; y < bottom; ++y)
        region.insert(region.end(), frame.begin() + y * width + left,
                      frame.begin() + y * width + right);
    compress(out, region);
    previous = frame;
    ++written;
}

bool GifWriter::close()
{
    if (hasPending)
        writeFrame(pending, pendingDelay);
    hasPending = false;
    out.put(char(0x3b));
    out.close();
    return !out.fail();
}
