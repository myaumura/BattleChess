#ifndef BC_GIF_WRITER_H
#define BC_GIF_WRITER_H

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

/* Native animated GIF89a encoder; no original entry point. Frames are 8-bit grayscale.
 * Identical consecutive frames are merged into one longer frame, and each written frame
 * stores only the rectangle that changed since the previous one. */
class GifWriter
{
public:
    bool open(const std::filesystem::path& path, int width, int height);

    /* gray holds width * height luminance bytes; delay is in hundredths of a second. */
    void addFrame(const std::vector<uint8_t>& gray, int delayCentiseconds);

    /* Flush the last frame and the trailer; returns false if any write failed. */
    bool close();

    int framesWritten() const
    {
        return written;
    }

private:
    std::ofstream out;
    int width = 0, height = 0, written = 0;
    std::vector<uint8_t> previous, pending;
    int pendingDelay = 0;
    bool hasPending = false;

    void writeFrame(const std::vector<uint8_t>& frame, int delayCentiseconds);
};

#endif
