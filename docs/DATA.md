# External game data

The application uses a prepared data pack outside the source repository. Supply your own Macintosh game resources and system fonts to the separate recovery tooling. Apple II data is incompatible. This repository includes neither those inputs nor the scripts that extract them.

The recovery baseline is MacPlay’s Battle Chess 1.0.2 for Motorola 68k Macintosh systems. Its embedded version notice reads “©1988-93 Interplay Productions.” The Macintosh release year in the README does not date this specific recovered build.

Use [BattleChess-re-tools](https://github.com/myaumura/BattleChess-re-tools) to prepare the pack from your own game HFS image and System MacBinary. Its [preparation guide and flow diagram](https://github.com/myaumura/BattleChess-re-tools/blob/main/docs/PREPARE_DATA.md) describe inputs, dependencies and the single-command workflow; its [recovery process](https://github.com/myaumura/BattleChess-re-tools/blob/main/docs/WORKFLOW.md) explains the analysis behind the data.

The recovery toolchain produces this layout:

```text
prepared-data/
  assets.hpp              Frame catalog
  original.hpp            Recovered geometry, UI resources, tables, and opening book
  bitmap_fonts.hpp        Chicago 12 and Geneva 9 bitmap strikes
  animation_data.hpp      Animation descriptors and scripts
  animation_pixels.bin    Cumulative two-bit animation pixels
  opening_book.bin        32,000-byte original book, used by the integration check
  assets/
    external_graphics/    Screens, standing pieces, and animation previews
    pictures/             Promotion pictures
    audio/                Recovered sound effects
    raw/CURS_400.bin       68-byte search cursor, including original hotspot
    raw/CURS_401.bin       68-byte in-check cursor, including original hotspot
    raw/System_CURS_4.bin  68-byte System watch cursor for blocked input
```

Older packs without `System_CURS_4.bin` must be regenerated with the updated recovery tools.

Catalogs and pixels must come from the same extraction. Renaming an original archive to one of these files is insufficient. The generated headers contain original resource data, so keep the entire pack outside this repository and do not add it to version control. A locally built application includes some of those resources in its compiled tables.

Set `BC_DATA_DIR` when building and use `--data PATH` when launching the executable directly. `tools/run.sh` uses the environment variable for both steps. Missing pack files stop configuration; missing runtime images or invalid animation pixel spans produce an error.

The current pack represents 646 animation sequences and 6,824 cumulative frames. Recovery completeness and original-runtime parity are separate: automatic idle selection/timing and stable original-game comparisons remain unfinished.
