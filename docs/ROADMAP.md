# Roadmap and feature coverage

Status recorded **2026-11-24**. The project targets the 1991 Macintosh release of Battle Chess for Motorola 68k processors, with a C11 engine and a C++17/SDL3 host for modern systems. It is playable with an external prepared data pack. Native test coverage and equivalence to the original game are separate milestones.

## Covered features

“Implemented” means the native path exists and has focused checks. It does not mean a complete original-session comparison has passed.

| Area | Current coverage | Validation boundary |
|---|---|---|
| Source repository | Root-level source, configuration, C/C++ tests, build/run scripts, MIT license and provenance notice; original data and recovery tooling kept separately | Source-only isolation build passes |
| Board and moves | Original board representation, legal move generation, check detection, captures, castling, en passant and four promotion choices | Core checks include initial-position move-tree counts of 20 / 400 / 8,902 |
| Computer player | Recovered opening-book traversal, evaluation, search, Human/Mac selection, move suggestions and Force Move | Native search/coordinator checks pass; original choices and shared RNG/event order still need comparison |
| Thinking time | Recovered fixed-level budgets, custom-minute entry and search-stop handling | Boundary checks and native UI smoke pass |
| Undo and replay | Native position snapshots and replay within the current session | Native checks pass; the original save format does not preserve session history |
| Rendering | Perspective and flat boards, original shape selection, anchors, hit testing, layering and two-bit side recoloring; native whole-pixel scaling (fractional only below 1x), high-density output and full screen | Initial scene has a scoped original capture comparison; full UI parity remains open; full screen and high-density scaling need manual desktop checks |
| Movement and combat | Recovered movement/combat graphs, turns, knight obstacles, rook transformations, fades, castling/en-passant submoves, promotion order and checkmate capture; native Settings → Animation Speed from 1x (original) to 2x | 113 native animation scenarios pass; original timing/audio/choreography comparison remains open |
| Animation data | 646 recovered sequences and 6,824 cumulative frames supported by the external pack | Saved pixels and regenerated data were checked; asset recovery does not prove every live call path |
| Side-by-side boards | Native opt-in view (Settings → Show Both Boards, `--both`): perspective board left, flat board right (cropped to its grid plus an 8-point margin) on an 882×452 canvas with an information panel underneath (status, players, level and a clickable SAN move list that scrolls to the latest move); moves can be entered on either board; the flat view holds the pre-move position until the perspective animation finishes; Set Up Board temporarily uses the single flat view | Native smoke check covers layout, cross-board move entry, animation hold, setup fallback and restoring the single view, plus the panel's move list, scrolling and click-to-jump history; the default single view and its screenshots are unchanged |
| Menus and dialogs | Macintosh-style menu tracking plus native click-to-open menus, recovered alerts/dialog resources, About rendering, player/view settings and button-release handling | Native resource/UI checks pass; complete window, menu, pressed and disabled appearance needs original captures |
| File handling | Open, Save and Save As; recovered 78-byte format; opaque-byte preservation; atomic writes; macOS `Game`/`IPBC` Finder identity | Native format and transfer checks pass; original-application exchange is unverified; interactive OS file pickers need manual coverage |
| PGN and GIF | Native File menu extensions: PGN export (SAN, FEN start, result), PGN import (first game, FEN start including en passant, comments/variations/annotations skipped, SAN and long algebraic moves resolved against legal moves) and animated GIF export that replays the session offscreen through the recovered animations on the 3D board (regardless of the current view) at twice the original speed, merging identical frames and storing changed rectangles | Core encoder/decoder round trip and PGN import/export checks; encoder output (LZW table resets, odd sizes, merged frames, sub-rectangles) decoded pixel-exact by ffmpeg and ImageMagick on 2026-11-24; native smoke imports a mated game, exports it from the flat view and with both boards shown, requires byte-identical 3D output and a presented progress dialog, and verifies the RNG stream, fade masks, session and animation state are unchanged. GIF export shows a progress dialog but blocks input while it runs, and a render failure mid-export leaves a partial file |
| Board setup | Piece placement/removal, Clear Board, position validation and acceptance/cancel paths | Core and native setup checks pass |
| Audio | Recovered sound events and effect selection with SDL playback and pitch adjustment | Dummy-audio tests do not verify audible output or original sound timing |
| Modem play | Recovered packets/checksums, retry/session logic, board/move/promotion/chat/quit paths and POSIX serial hosting | Native pseudo-terminal checks pass; original peer and physical modem compatibility are unverified |
| Idle animations | Twelve recovered bishop scripts / 218 frames, with duplicate entries and a bounded caller audit | Data recovery only: no proven selection/time gate; automatic idle playback is not implemented |

Original graphics, sound, fonts, resource tables, archives and disk images are not shipped here. The [data contract](DATA.md) describes the external inputs.

## Validation status

| Target | Recorded result | Remaining platform validation |
|---|---|---|
| macOS core | 20/20 tests in a Debug build on 2026-11-24, including PGN import/export and GIF encoder checks; last ASan/UBSan run 17/17 on 2026-10-03, including a stale app-cache regression | Broader compiler/architecture coverage |
| macOS application | Last recorded app build: 23/23 C/C++ tests with ASan/UBSan on 2026-10-03; native smoke and 113 animation scenarios included. Current Release app build: 27/27 tests on 2026-11-24 (no sanitizers); the native smoke covers PGN import, GIF export and its progress dialog, click-to-open menus, both boards with the move panel, and the full-screen toggle. Actual-host serial checks are retained in the reverse workspace | Manual desktop/file-picker/audio checks, full screen and Retina scaling on real displays, and full original-runtime parity |
| Linux | Earlier recovery milestone passed 29 tests, X11/Xvfb smoke and 113 animation scenarios on 2026-10-02 with Ubuntu, GCC and SDL3 | Repeat against the current root layout and external-data build; historical test counts include recovery checks now kept separately |
| Other Unix systems | POSIX-oriented source and CMake setup are present | No current execution result recorded |
| Original Macintosh game | A saved initial-scene comparison matched 163,370 visible content pixels at 2×, excluding the original cursor | Stable completed gameplay reference sessions, About investigation, decisions, timing/audio and interoperability |

Original resources and detailed recovery captures/logs remain in the separate reverse-engineering workspace. No new Linux, emulator or physical-device execution is claimed by this documentation update.

## Next milestones

1. **Validate the current Linux layout.** Build the core alone and the app against an external pack, then run CTest and desktop/X11 smoke. Record compiler, SDL version, architecture and any warnings. Completion requires results for the current source rather than carrying forward the earlier milestone.
2. **Establish stable original reference gameplay.** Investigate the original About failure and unfinished turn observation. Completion requires a reproducible original setup that finishes a human/computer turn, with configuration and observations saved outside this repository.
3. **Compare computer play, animation and audio.** Use identical positions, settings and move sequences for opening-book play, hints, Force Move and timed search. Cover both sides of movement/combat, knight obstacles, rook transformations, castling, en passant, promotion and mate. Record decisions, RNG/event order, frames, positions, fades, sound events and duration; document each difference before changing behavior.
4. **Finish window/control appearance checks.** Compare menus, window geometry, pressed/disabled buttons and About under the original environment. Preserve documented native platform adaptations and keep each pixel comparison's scope explicit.
5. **Verify save exchange with the original application.** Load a native-written save in the original and an original-written save in the native port. Confirm position, side and controller settings; do not require history absent from the original 78-byte format. Container/HFS transfer success alone does not complete this milestone.
6. **Resolve idle-animation reachability.** Recover the actual caller, piece selection and scheduling conditions before enabling playback. If the scripts are unused in this Macintosh version, document that conclusion with evidence. No invented timer or random-piece trigger is planned.
7. **Verify original and physical modem interoperability.** Capture move, board, promotion, chat, retry and quit exchanges with an original peer; then test physical dialing/flow control separately. A native pseudo-terminal pass cannot replace either result.
8. **Add native network play.** Multiplayer currently means two humans on one machine (Settings → Human White and Human Black) or the recovered modem session over a POSIX serial device (`--serial DEVICE`); there is no internet option. Two routes are planned:
   - *Serial-over-TCP bridge (no code change):* expose a pseudo-terminal per player with `socat` (for example `socat pty,link=/tmp/bc-serial,raw,echo=0 tcp-listen:PORT` on one host and `... tcp:HOST:PORT` on the other) and pass it to `--serial`. Untested: Dial Number and Set Auto Answer send Hayes `AT` commands, which reach the peer application instead of a modem, so session start-up must be checked with two instances before documenting the commands.
   - *Built-in TCP transport:* add native options such as `--host PORT` and `--connect HOST:PORT` that carry the existing recovered packets, checksums and retry logic over a socket and skip the `AT` dialing layer. Keep it a separate native adapter so the recovered modem session stays unchanged.

   Completion requires a full game between two networked instances covering moves, board setup, promotion, chat and quit on both sides, plus a documented behavior for dropped connections. It does not replace the original-peer comparison in milestone 7.
9. **Port the application to iPadOS.** SDL3 supports iOS, the renderer already scales the 511×320 content by whole pixels at full display density, and SDL delivers touches as mouse events, so board input and the click-to-open menus need no new input model. Required native adaptations:
   - *SDL3:* the Homebrew package is macOS-only; build SDL3 for iOS from source and select it with `CMAKE_PREFIX_PATH`.
   - *Build:* add an iOS configuration (`CMAKE_SYSTEM_NAME=iOS`, Xcode generator) producing an app bundle with `Info.plist` and icon.
   - *Data:* `--data` is unavailable; copy the external pack (about 69 MB) into the locally built bundle and resolve it from `SDL_GetBasePath()`. The pack stays outside the repository as today.
   - *Cursors:* `loadCursor` requires `SDL_CreateCursor` to succeed; skip the original cursors where the platform has no pointer cursor.
   - *Files:* SDL3's native file dialogs are believed unavailable on iOS (verify); save, open, PGN import/export and GIF export need the app's Documents folder (exposed in the Files app) or a native document picker.
   - *Modem:* no serial devices exist on iPadOS; hide modem play until the TCP transport from milestone 8 is available.
   - *Shortcuts:* Command shortcuts work only with a hardware keyboard; every action remains reachable from the menus.
   - *Layout:* the both-boards canvas (882×452) is too wide for portrait; use the stacked layout from milestone 12 there.

   The bundle embeds original resources, so builds are for personal installation from Xcode only (free Apple ID provisioning expires after 7 days; a paid developer account lasts one year), never App Store or TestFlight distribution. Completion requires a full game on a physical iPad or the Simulator covering board input, menus, promotion, setup, audio and save/load, with the run recorded in the validation table.
10. **Integrate Lichess.** Add optional online play and analysis through the public [Lichess API](https://lichess.org/api) as a native extension, separate from the recovered engine and modem code.
    - *Analysis (smallest step):* upload the current game's PGN (from File → Export PGN) through the Lichess import endpoint and open the returned analysis URL in the browser. No account is needed.
    - *Online play:* use the Board API for the user's own human account: authenticate with a personal OAuth token, stream incoming game events, send moves in UCI notation and present opponent moves through the same animation path as modem moves. Seeks, challenges, resign, draw offers and aborts map to new menu items. Lichess board-API rules (permitted time controls, rate limits, token scopes) must be checked against the current documentation before implementation.
    - *Fair play:* while a Lichess game is active, disable Suggest Move, Force Move and every Mac-controlled side; the recovered engine must never move or advise for the user's account.
    - *Clocks and results:* the original interface has no clock, so display both players' remaining time, and handle flagging, disconnects, abort and server-declared results.
    - *Platform:* requires a native HTTPS/TLS client with streaming (libcurl or platform APIs) and secure token storage (macOS Keychain); no credential is written to save files or logs.

    Completion requires a full casual game against another account covering normal moves, castling, en passant, promotion, resignation and a timeout, plus a successful analysis import. Network play from milestone 8 stays independent of Lichess.
11. **Polish GIF export.** The current export is complete but minimal.
    - *Responsiveness:* render frames incrementally from the event loop with a Cancel button, instead of blocking input behind the progress dialog until the file is written.
    - *Failure cleanup:* write to a temporary file and rename on success, so an interrupted or failed export never leaves a partial GIF (PGN and save files already use atomic writes).
    - *Options:* offer export speed (reusing the Animation Speed choices) and scale (1x/2x), independent of the live settings; the export stays on the 3D board.

    Completion requires a native check that cancels an export mid-run, confirms no file remains and the live state is unchanged, and a manual long-game export with the window kept responsive.

12. **Refine the screen layout.** Both boards with the move panel and whole-pixel full screen are in place; remaining ideas build on that layout without changing the default single view.
    - *Captured pieces:* show each side's captured pieces in the panel. The flat-piece artwork is board-square sized, so this needs a small native piece set drawn at 1x rather than downscaled sprites.
    - *Board coordinates:* label files a–h and ranks 1–8 in the flat board's margin so the move list reads against the board.
    - *Stacked layout:* place the flat board and panel under the 3D board when the window is taller than wide (and on iPad in portrait).
    - *Larger 3D board:* at high whole-pixel scales, draw the 3D board one step larger than the flat board, since it carries the animations.
    - *Full-screen backdrop:* confirm the color around the canvas in full screen on macOS and Linux, and choose a Mac-desktop gray or black deliberately.

    Completion requires smoke checks for each layout's hit testing and panel contents, plus manual screenshots at 1x, 2x and full screen on a Retina and a standard display.

These are completion criteria, not release dates. New translations should retain original function addresses, explain their purpose and assumptions, and add focused checks. Keep fresh builds, saved-output inspection, native execution, original comparisons and physical-device results distinct.

## Project boundaries

The repository supports user-supplied data; it does not distribute original game/system resources or extraction tooling. The separate reverse workspace retains the broad Ghidra export and its unresolved decompiler failures; completing that export is a different task from validating a native feature.

This is not a clean-room implementation. The [MIT license](../LICENSE) covers project contributions within the scope described in [NOTICE](../NOTICE), and does not grant rights to original or third-party material.
