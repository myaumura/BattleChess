# Building on macOS and Linux

Run the project commands below from the repository root. `tools/build.sh` and `tools/run.sh` also work when called by absolute path from another directory.

## Requirements

| Build | Requirements |
|---|---|
| Core and core tests | C11/C++17 compiler, CMake 3.20+, POSIX system |
| Playable application | Core requirements, SDL3 3.4+ development files, prepared external game data, desktop video/audio environment |

All repository tests are C/C++; Python is not required for either build. The core builds without SDL or original game files. The application needs the [external data pack](DATA.md); this repository does not extract or download it. Original Macintosh resources, graphics, sound, and fonts remain outside this checkout.

## macOS

Install Apple's Command Line Tools if they are not already installed:

```sh
xcode-select --install
```

With Homebrew installed, install CMake and build the core:

```sh
brew install cmake
./tools/build.sh
```

For the application, also install SDL3:

```sh
brew install sdl3
```

Then follow [Build and run the application](#build-and-run-the-application) below. Use the compiler's normal host architecture; no original Macintosh emulator is required to run the native application.

## Linux

The following package commands use Debian/Ubuntu names. On another distribution, install equivalent compiler and CMake packages.

For the core:

```sh
sudo apt update
sudo apt install build-essential cmake pkg-config
./tools/build.sh
```

For the application, install your distribution's SDL3 development package. On Debian/Ubuntu releases that provide SDL3 3.4+:

```sh
sudo apt install libsdl3-dev
pkg-config --modversion sdl3
pkg-config --atleast-version=3.4.0 sdl3
```

The last command must succeed. Package availability and versions depend on the distribution release. If the available package is older or missing, install SDL3 3.4+ development files separately and use [a custom SDL installation](#custom-sdl-installation). The project does not build SDL itself.

Then follow the application commands below. Interactive play needs a desktop session with a working SDL video backend. A headless test pass does not verify desktop interaction or audible sound.

## Build and run the application

Prepare a matching data pack with the separate recovery toolchain, then set its absolute directory. A copied original game archive alone is insufficient; [DATA.md](DATA.md) lists the required files.

```sh
export BC_DATA_DIR=/absolute/path/to/prepared-data
./tools/build.sh -DBC_BUILD_APP=ON -DBC_DATA_DIR="$BC_DATA_DIR"
./tools/run.sh
```

`build.sh` configures a Debug build, compiles it, and runs CTest. It selects the core by default; the explicit app option above overrides that default. `run.sh` configures and builds the app before launching, using `BC_DATA_DIR` for both compilation and runtime data. Switching packs rebuilds the matching compiled catalogs.

Application options can be passed to the run script:

```sh
./tools/run.sh --flat
./build/battlechess --data "$BC_DATA_DIR" --help
```

If you relocate the executable, retain the external pack and pass its path with `--data`. Copy `battlechess-icon.png` beside the executable to retain its optional window icon. Some original resource data is compiled into the local application; generated packs and data-dependent executables are outside this source repository's distribution.

## Custom SDL installation

Point CMake at the prefix containing `lib/cmake/SDL3/SDL3Config.cmake`:

```sh
export SDL3_PREFIX=/absolute/path/to/sdl3/install
./tools/build.sh -DBC_BUILD_APP=ON -DBC_DATA_DIR="$BC_DATA_DIR" \
    -DCMAKE_PREFIX_PATH="$SDL3_PREFIX"
./tools/run.sh
```

CMake retains that prefix in the build cache. Use `-DSDL3_DIR=/absolute/path/to/lib/cmake/SDL3` instead if you need to select the package directory directly.

## Direct CMake and release builds

For a separate core build:

```sh
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Debug -DBC_BUILD_APP=OFF
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

For a release application:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
    -DBC_BUILD_APP=ON -DBC_DATA_DIR="$BC_DATA_DIR"
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
./build-release/battlechess --data "$BC_DATA_DIR"
```

Direct CMake commands retain cached options. The shell scripts use `build/`; separate directories keep other configurations independent. Assertion-based checks remain enabled in Release builds.

## Checks and troubleshooting

Enable AddressSanitizer and UndefinedBehaviorSanitizer with Clang or GCC:

```sh
./tools/build.sh -DBC_SANITIZE=ON
./tools/build.sh -DBC_BUILD_APP=ON -DBC_DATA_DIR="$BC_DATA_DIR" -DBC_SANITIZE=ON
```

The core registers 17 C/C++ tests. With the application enabled, there are 23. Python-based host/recovery checks are kept in the separate reverse workspace. The app's automated SDL checks use dummy video/audio drivers; CTest sets them automatically. Run those checks with `ctest --test-dir build --output-on-failure` after building the app.

- **SDL3 package missing or too old:** install SDL3 3.4+ development files and set its CMake prefix. Check the version selected in CMake's configure output.
- **`BC_DATA_DIR` missing or incomplete:** select a prepared pack containing all files listed in [DATA.md](DATA.md), outside the source repository.
- **Runtime data missing:** the path passed with `--data` must refer to the same pack used at build time, including its `assets/` directory and animation pixels.
- **No video device during interactive play:** use a desktop session with an SDL-supported backend. Automated dummy-driver checks exercise native logic and rendering but do not open an interactive window.

See the [roadmap and validation status](ROADMAP.md#validation-status) for what has actually been tested on each platform. These instructions do not claim a fresh Linux build of the current source.
