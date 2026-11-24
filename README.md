<p align="center"><img src="resources/icon.png" alt="BattleChess knight icon" width="128"></p>

# BattleChess

An unofficial native recompilation of **Battle Chess for the Macintosh (1991)**, the black-and-white game whose pieces fight each other on capture. The original Motorola 68k program was reverse-engineered; its rules, opening book, computer player, save format and animations run again as C11 code, hosted by a C++17/SDL3 application on modern macOS and Linux. Original game data is supplied separately.

This repository is a fork of Kirill Gusev's port ([myaumura/BattleChess](https://github.com/myaumura/BattleChess)). The fork keeps the recovered game intact and adds what a modern copy needs: **online play on Lichess**, both boards side by side with a move list, **PGN** import/export, **animated GIF** recordings, sharp full-screen scaling, click-to-open menus, and a finished board that stays put. The recovered engine, computer player and animation logic are unchanged; each addition is either opt-in or listed below as a deliberate difference from the original.

This is a reverse-engineering project, not a clean-room implementation or an official release.

## Screenshots

These are captures of the native application with an external data pack, not mockups. They show original game artwork, which is outside the MIT license.

| Perspective board | Flat board |
|---|---|
| ![Native perspective board](resources/perspective.png) | ![Native flat board](resources/flat.png) |

![Pawn combat in the native animation runtime](resources/combat.png)

**Both boards with the move list** (Settings → Show Both Boards): the 3D board animates while the 2D board and the panel show the position, players and clickable moves.

![Both boards side by side with the status and move-list panel](resources/both-boards.png)

**Lichess:** the lobby of running games and challenges, and an online game with both clocks in the menu bar.

| Lichess lobby | Online game |
|---|---|
| ![Lichess lobby listing games and new-game buttons](resources/lichess-lobby.png) | ![Online game with clocks and the Lichess menu](resources/lichess-game.png) |

**GIF export** (File → Export GIF...): a whole game replayed through the original animations, here the four-move mate, recorded by the application itself.

![Animated GIF of a four-move checkmate exported by the application](resources/gif-export.gif)

## What this fork adds

| Feature | What it does |
|---|---|
| **Lichess online play** | Connect with a Lichess token, then play the Lichess AI, challenge a friend by username or find an opponent. A lobby lists running games and incoming challenges. Clocks sit in the menu bar; draw offers, resign/abort and leaving a game without resigning are supported. The recovered engine never moves or advises for your account. Animations adapt to the clock and a click skips them. |
| **Analyse on Lichess** | Uploads the current game and opens Lichess's analysis board; no account needed. |
| **Both boards + move panel** | Shows the 3D and 2D boards side by side, with whose turn it is, the players and a clickable move list that jumps through the game. |
| **PGN import/export** | Writes the session's moves as PGN (with a FEN tag for set-up positions); loads the first game of a PGN file and lets Take Back/Replay step through it. |
| **Animated GIF export** | Replays the game offscreen through the original movement, combat and checkmate animations at 2x into a GIF, with a progress dialog. |
| **Sharp scaling and full screen** | Whole-pixel scaling keeps the 1-bit artwork crisp, at full Retina density; full screen with F11 or Control-Command-F. |
| **Click-to-open menus** | Menus also open with a click and stay open; the original press-drag-release still works. |
| **Finished games stay** | The final position remains after the ending message, for review, Take Back and export, until New Game. |
| **Idle animations (opt-in)** | Lets a bishop play the recovered fidget clips when the board is quiet. |
| **Research** | Shows from the disassembly that the Mac release never loads its idle animations ([evidence](docs/ROADMAP.md#idle-animations-in-the-macintosh-release)). |
| **Tests** | Adds five test programs: PGN export, PGN import, GIF encoder, Lichess protocol, and a Lichess client against a local fake server. The native smoke test also covers every feature above; nothing needs a network or an account. |

The upstream port already provided the recovered game itself: engine, opening book, animations and sound, save/load, board setup, modem play over a serial device, the original cursors, and the 1x–2x animation speed setting.

## Differences from the original 1991 game

Everything not listed here follows the recovered original code: move rules, computer search and opening book, animation choreography, sounds, the 78-byte save format and the modem protocol.

| Area | Original Macintosh game | This port |
|---|---|---|
| Menus | Press, drag, release | Also click to open, click to choose |
| End of game | Board cleared after the ending message | Final position stays until New Game (modem games still clear, so both machines stay in step) |
| Display | Fixed 512×342 screen | Resizable window, whole-pixel scaling, full screen, optional both-boards view |
| Animation speed | Fixed | 1x to 2x |
| Idle bishops | Data present, never played | Optional, off by default |
| Opponents | Computer, same-machine human, modem | Same, plus Lichess (AI, friends, public seeks) |
| Files | Own save format | Same, plus PGN import/export and GIF export |
| Platform | Macintosh Toolbox | SDL3 window, audio and input; native file dialogs; macOS Keychain for the Lichess token |

## Getting started

You need:
- a C11/C++17 compiler and CMake 3.20+;
- SDL3 3.4+ development files;
- libcurl for Lichess (included with macOS; `libcurl4-openssl-dev` on Debian/Ubuntu; optional);
- a **prepared data pack** made from your own copy of the game with [BattleChess-re-tools](https://github.com/myaumura/BattleChess-re-tools/blob/main/docs/PREPARE_DATA.md). This repository contains no original binaries, disk images, graphics, sound or fonts; [docs/DATA.md](docs/DATA.md) lists what the pack must contain.

```sh
export BC_DATA_DIR=/absolute/path/to/prepared-data
./tools/build.sh -DBC_BUILD_APP=ON -DBC_DATA_DIR="$BC_DATA_DIR"   # build and test
./tools/run.sh                                                    # build and play
```

Or with CMake directly:

```sh
cmake -S . -B build -DBC_BUILD_APP=ON -DBC_DATA_DIR="$BC_DATA_DIR"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/battlechess --data "$BC_DATA_DIR"
```

`./tools/build.sh` on its own builds and tests just the engine core, which needs neither SDL nor game data; add `-DBC_SANITIZE=ON` for AddressSanitizer and UndefinedBehaviorSanitizer. [docs/BUILD.md](docs/BUILD.md) covers dependencies, custom SDL paths, release builds and troubleshooting.

## How to play

Click a piece, then its destination. Human White against Mac Black is the default; change the players under Settings and the computer's strength under Level. Promotion offers the recovered piece choices after the pawn moves. Move → Take Back and Replay step through the game; Suggest Move outlines a hint and Force Move makes the computer move now. File → Set Up Board places pieces freely for a custom starting position.

| Keys (Command on macOS, Ctrl elsewhere) | Action |
|---|---|
| N / O / S | New Game / Open / Save |
| B / R | Take Back / Replay |
| F / M | Force Move / Suggest Move |
| E / I / G | Export PGN / Import PGN / Export GIF |
| T | Talk (modem games) |
| Q | Quit |
| F11 or Control-Command-F | Full screen |
| Escape | Close a menu, selection or dialog |

| Command-line option | Effect |
|---|---|
| `--data PATH` | Prepared data pack (`run.sh` passes `BC_DATA_DIR`) |
| `--flat` | Start with the flat 2D board |
| `--both` | Start with both boards side by side |
| `--serial DEVICE` | Connect a POSIX serial device for modem play |
| `--help` | List options |

### Views and animations

- **Settings → Switch to 2D/3D Board** changes the view; **Show Both Boards** shows both, with the panel underneath. Moves can be made on either board, and clicking a move in the list jumps there. The 3D board animates while the 2D board waits, then catches up.
- **Settings → Animation Speed** plays movement, combat and capture fades at 1x (original), 1.25x, 1.5x, 1.75x or 2x.
- **Settings → Enter Full Screen** fills the display; the picture is always scaled by whole pixels unless the window is smaller than the game.
- **Settings → Idle Animations** (off by default) lets a bishop fidget after 10–25 quiet seconds on a human's turn; any click or key stops it. The clips are the game's own, but the trigger and timing are this port's invention.

### Files, PGN and GIF

- **File → Save / Open** use the original save format (with the Mac `Game`/`IPBC` file identity on macOS). The original format stores only the position, so move history is not saved.
- **File → Export PGN...** writes the moves since the last New Game, Open or board setup; **Import PGN...** loads the first game of a file at its final position. Comments, variations and annotations are skipped.
- **File → Export GIF...** records the game on the 3D board at twice the original speed and double size. A progress dialog shows the move being recorded; the window accepts no input until it finishes.
- When a game ends (checkmate, stalemate, the computer resigning, or a Lichess result), the final position stays on the board for these exports and for Take Back until **File → New Game**.

### Play on Lichess

The **Lichess** menu (in the Modem slot when no serial device is connected) plays casual games on [lichess.org](https://lichess.org) through its Board API, showing the opponent's moves through the game's own animations.

1. **Connect...** opens Lichess's token page with the needed scopes ticked (`board:play`, `challenge:read`, `challenge:write`). Create the token, paste it into the prompt (Command/Ctrl+V) and press OK. On macOS it is kept in the login Keychain; elsewhere it lasts for the session, or set `BC_LICHESS_TOKEN`. **Forget Lichess Token** removes it. The token is only ever sent to Lichess.
2. **Games...** opens the lobby. It also opens by itself after connecting when games are running, and when someone challenges you. It lists incoming challenges and your games in progress (opponent, speed, whose move, time left), most urgent first:
   - **Open** (or double-click, or Return) plays the selected game here.
   - **Accept** and **Decline** answer a challenge.
   - Bullet games, which the Board API cannot play, open on lichess.org instead.
   - **Time** picks 10+0, 15+10, 30+0, 30+20, 5+3 (blitz: AI and challenges only) or 3-day correspondence.
   - **Play AI** uses the Level menu's strength (Novice and Level 1 = AI level 1, up to Level 8 and 9 = AI level 8).
   - **Challenge...** invites a friend by Lichess username; they accept on lichess.org, in the Lichess app, or in Battle Chess.
   - **Find Opponent** posts a public seek (rapid, classical and correspondence only).

   You play White or Black when Settings has exactly one human side; otherwise the colour is random.
3. **During a game:**
   - Both clocks run in the menu bar; correspondence games show whose move it is.
   - Draw offers arrive as Accept/Cancel alerts. **Offer Draw** and **Resign** (or **Abort Game** before both sides have moved) are in the menu.
   - **Leave Game** returns to a local board without resigning; the game continues on Lichess with your clock running, and the lobby reopens it.
   - Suggest Move, Force Move, Take Back, New Game, the player settings and the levels are disabled.
4. **Animations** sets how much clock time animations may take:
   - *Auto* (the default) plays them fully in classical and correspondence games.
   - In rapid, *Auto* plays quiet moves at 2x and shows captures at once.
   - In blitz, or with under a minute on your clock, *Auto* shows moves at once.
   - *Full*, *Fast* and *Board Only* fix one style.
   - A click during an animation finishes it immediately, and with both boards shown the 2D board shows the new position at once.
5. **Analyse Game on Lichess** uploads the current game for analysis, without an account. After an online game it opens that game's page.

Not yet supported:
- playing from Black's side of the board (the view stays White's);
- chat and takebacks;
- claiming a win when the opponent leaves;
- rated games.

## Tests

All tests are C/C++; Python is not required. The core registers 22 tests (20 without libcurl); with the application enabled there are 29. They cover:
- the recovered engine (move generation, search, evaluation, save format, board setup, time control) and the animation runtime, with 113 animation scenarios;
- PGN, the GIF encoder (its output was also checked against ffmpeg and ImageMagick), and the Lichess protocol and client;
- a native smoke test that drives the application headlessly through menus, dialogs, both boards, exports, idle animations and a whole Lichess game with injected server replies.

The SDL checks use dummy video and audio drivers, so passing tests do not prove audible sound, and they do not prove equivalence to the original executable.

## Status

Development of the fork's features is essentially complete. Open items, tracked in [docs/ROADMAP.md](docs/ROADMAP.md):
- recorded games against the real lichess.org;
- a flipped board when playing Black;
- Lichess chat and takebacks;
- Linux token storage;
- a Linux build pass;
- the long-running comparisons against the original game (computer choices, animation timing, audio, save and modem exchange with the original application).

## Project layout

```text
config/    Build and formatting configuration
lib/       Recovered C engine and C++ presentation code
src/       SDL application, native adapters and fork extensions
           (Lichess*, Json, Pgn*, GifWriter, IdleAnimator, ApplicationPanel, ...)
resources/ App icon and screenshots
tests/     C/C++ core and integration checks
tools/     Build and run scripts
docs/      Build guide, code style, roadmap and data requirements
```

Recovered functions keep their original addresses and recovery notes in comments; native adapters and fork extensions are marked as such. Decompiler exports, Ghidra projects and recovery scripts live with [BattleChess-re-tools](https://github.com/myaumura/BattleChess-re-tools), not here.

## Documentation

- [Build on macOS and Linux](docs/BUILD.md)
- [Roadmap and feature coverage](docs/ROADMAP.md): what is implemented, how it was validated, and what remains
- [External game data](docs/DATA.md)
- [Code style](docs/CODE_STYLE.md)
- [BattleChess-re-tools](https://github.com/myaumura/BattleChess-re-tools): data preparation and the [recovery process](https://github.com/myaumura/BattleChess-re-tools/blob/main/docs/WORKFLOW.md)

## Credits and license

The native recompilation is the work of Kirill Gusev (upstream [myaumura/BattleChess](https://github.com/myaumura/BattleChess)); data preparation and the recovery process come from [BattleChess-re-tools](https://github.com/myaumura/BattleChess-re-tools). This fork builds on both. Online play uses the public [Lichess API](https://lichess.org/api).

Project contributions use the [MIT license](LICENSE). The project is unofficial and not affiliated with or endorsed by the original rights holders. Original game and third-party material, including the artwork shown in the screenshots, are outside that license; see [NOTICE](NOTICE).
