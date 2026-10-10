#include "SDLHelpers.h"
#include <cassert>
#include <fstream>
#include <stdexcept>

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(SDL_Init(SDL_INIT_VIDEO));
    {
        auto thinking = loadCursor(fs::path(argv[1]) / "assets/raw/CURS_400.bin");
        auto checked = loadCursor(fs::path(argv[1]) / "assets/raw/CURS_401.bin");
        auto watch = loadCursor(fs::path(argv[1]) / "assets/raw/System_CURS_4.bin");
        assert(SDL_SetCursor(thinking.get()));
        assert(SDL_GetCursor() == thinking.get());
        assert(SDL_SetCursor(checked.get()));
        assert(SDL_GetCursor() == checked.get());
        assert(SDL_SetCursor(watch.get()));
        assert(SDL_GetCursor() == watch.get());

        const auto path = fs::temp_directory_path() /
                          ("battlechess-cursor-" + std::to_string(SDL_GetTicksNS()) + ".bin");
        struct RemoveFixture {
            fs::path path;
            ~RemoveFixture() {
                std::error_code error;
                fs::remove(path, error);
            }
        } removeFixture{path};
        auto rejects = [&](const char *reason) {
            try {
                auto cursor = loadCursor(path);
                assert(false && "Malformed cursor accepted");
            } catch (const std::runtime_error &error) {
                assert(std::string(error.what()).find(reason) == 0);
            }
        };
        rejects("Missing ");
        std::vector<char> bytes(68, 0);
        auto write = [&]() {
            std::ofstream out(path, std::ios::binary);
            out.write(bytes.data(), bytes.size());
            assert(out.good());
        };
        for (size_t size : {size_t(0), size_t(67), size_t(69)}) {
            bytes.resize(size);
            write();
            rejects("Invalid Macintosh cursor size: ");
        }
        bytes.resize(68);
        for (int offset : {64, 65, 66, 67}) {
            bytes[offset] = char(255);
            write();
            rejects("Invalid Macintosh cursor hotspot: ");
            bytes[offset] = 0;
        }
        bytes[65] = 5;
        bytes[67] = 7;
        write();
        auto valid = loadCursor(path);
        assert(SDL_SetCursor(valid.get()));
        assert(SDL_GetCursor() == valid.get());
    }
    assert(SDL_GetCursor() == SDL_GetDefaultCursor());
    SDL_Quit();
}
