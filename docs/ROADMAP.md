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
| Undo and replay | Native position snapshots and replay within the current session; finished games stay on the board (for export and review) until New Game, where the original reset after the ending alert (kept for modem games) | Native checks pass; the original save format does not preserve session history |
| Rendering | Perspective and flat boards, original shape selection, anchors, hit testing, layering and two-bit side recoloring; native whole-pixel scaling (fractional only below 1x), high-density output, full screen, board coordinates and a captured-pieces strip (Settings toggles, on by default), and a board rotatable by 180 degrees, by hand or automatically towards the player to move (two humans), the human side (against the Mac) or the player's colour (Lichess); the 2D board always follows, the 3D board with Rotate 3D Board Too (the recovered planner runs on the turned, side-swapped position and sprites are recolored back) | Initial scene has a scoped original capture comparison, made before the native coordinates and captured strip (switch both off to compare); full UI parity remains open; full screen and high-density scaling need manual desktop checks |
| Movement and combat | Recovered movement/combat graphs, turns, knight obstacles, rook transformations, fades, castling/en-passant submoves, promotion order and checkmate capture; native Settings → Animation Speed from 1x (original) to 2x | 113 native animation scenarios pass; original timing/audio/choreography comparison remains open |
| Animation data | The recovery records count 646 sequences and 6,824 cumulative frames; the prepared catalog used by the app lists 967 clips and 7,145 frames in 33 groups, including the `combat_archive1_*` groups (counted 2026-11-24). The two counts use different units and have not been reconciled | Saved pixels and regenerated data were checked; asset recovery does not prove every live call path |
| Side-by-side boards | Native opt-in view (Settings → Show Both Boards, `--both`): perspective board left, flat board right (cropped to its grid plus an 8-point margin) on an 882×478 canvas with an information panel underneath (status, players, level and a clickable SAN move list that scrolls to the latest move); moves can be entered on either board; the flat view holds the pre-move position until the perspective animation finishes (during Lichess games it shows the new position at once); a captured-pieces row closes the panel; Set Up Board temporarily uses the single flat view; in full screen or an enlarged window the space beyond the canvas at the whole-pixel scale is filled (flat board centred in the extra width, longer move list) instead of letterboxed | Native smoke check covers layout, cross-board move entry, animation hold, setup fallback and restoring the single view, plus the panel's move list, scrolling and click-to-jump history, the panel with captured pieces, and filling an enlarged window (centred flat board, extra move rows, hit testing) |
| Menus and dialogs | Macintosh-style menu tracking plus native click-to-open menus, recovered alerts/dialog resources, About rendering, player/view settings and button-release handling | Native resource/UI checks pass; complete window, menu, pressed and disabled appearance needs original captures |
| File handling | Open, Save and Save As; recovered 78-byte format; opaque-byte preservation; atomic writes; macOS `Game`/`IPBC` Finder identity | Native format and transfer checks pass; original-application exchange is unverified; interactive OS file pickers need manual coverage |
| PGN and GIF | Native File menu extensions: PGN export (SAN, FEN start, result), PGN import (first game, FEN start including en passant, comments/variations/annotations skipped, SAN and long algebraic moves resolved against legal moves) and animated GIF export that replays the session offscreen through the recovered animations on the 3D board (regardless of the current view) at twice the original speed, merging identical frames and storing changed rectangles | Core encoder/decoder round trip and PGN import/export checks; encoder output (LZW table resets, odd sizes, merged frames, sub-rectangles) decoded pixel-exact by ffmpeg and ImageMagick on 2026-11-24; native smoke imports a mated game, exports it from the flat view and with both boards shown, requires byte-identical 3D output and a presented progress dialog, and verifies the RNG stream, fade masks, session and animation state are unchanged. GIF export shows a progress dialog but blocks input while it runs, and a render failure mid-export leaves a partial file |
| Board setup | Piece placement/removal, Clear Board, position validation and acceptance/cancel paths | Core and native setup checks pass |
| Audio | Recovered sound events and effect selection with SDL playback and pitch adjustment | Dummy-audio tests do not verify audible output or original sound timing |
| Lichess | Native Lichess menu over the Board API: token connect (macOS Keychain), a lobby listing running games and incoming challenges (`/api/account/playing` plus the event stream) with Open/Accept/Decline and new-game buttons, Leave Game without resigning, PGN analysis upload, games against the Lichess AI, challenges, public seeks, incoming challenges, draw offers, resign/abort, clocks in the menu bar, clock-aware animation policy (Auto/Full/Fast/Board Only) with click-to-skip and an immediately updated 2D board, which turns to Black's side when playing Black; the recovered engine, hints and local-only commands are disabled during online games | Core checks cover JSON, UCI (both castling forms, promotion), move-list sync and the animation policy; a client test against a local fake server covers headers, the one-at-a-time queue, 429 pause and NDJSON streams; the native smoke drives a whole game with injected replies. No game against the real lichess.org has been recorded yet |
| Modem play | Recovered packets/checksums, retry/session logic, board/move/promotion/chat/quit paths and POSIX serial hosting | Native pseudo-terminal checks pass; original peer and physical modem compatibility are unverified |
| Idle animations | Twelve recovered bishop scripts / 218 frames; the Macintosh release never reads them ([evidence](#idle-animations-in-the-macintosh-release)). Native opt-in Settings → Idle Animations plays them on a bishop whose resting pose matches the clip, after 10–25 s of quiet on a human turn, with its own random generator | Native smoke covers default-off, quiet period, bishop and pose matching, frame advance, clip end, input interrupt, the gate (flat view, Mac turn, menu, alert) and the untouched shared RNG; trigger and timing are native, not original |

Original graphics, sound, fonts, resource tables, archives and disk images are not shipped here. The [data contract](DATA.md) describes the external inputs.

### Idle animations in the Macintosh release

Static analysis of the 1.0.2 executable (CODE resources disassembled with `resource_dasm`, 2026-11-24) shows the idle block in ALLCANM1 is never read, so the original cannot play it:

- All archive access goes through the C library's file layer and one `PBReadSync`; there is no `SetFPos`. Every seek takes its offset from one of five tables whose pointers are set once at startup.
- The ALLCANM1 table holds three metadata/pixel pairs: standing pieces (0, 192), idle (26903, 27332) and flat pieces (37520, 37544). Its only reader, LOADBOAR, seeks to entry 0 (3D) or entry 4 (2D), never entry 2.
- The idle offsets appear nowhere else in the initialized application data. The only read without its own seek is the pixel decoder that follows a just-loaded header; after the standing set's 192-byte header it stops at 26903, where the idle block begins, and the 429-byte idle header is never loaded.
- The clips match bishops exactly: a resting bishop's display variants 1, 3, 5 and 7 map to standing shapes 0–3, the scripts' base poses.

Limits: the original was not run, and a negative index into a neighbouring table could in principle alias the idle entry; no such index was found. The earlier direct-word audit (BattleChess-re-tools, 2026-10-03) found no caller either. The assets are most likely carried over from another platform's version; the native setting is an explicit extension.

## Validation status

| Target | Recorded result | Remaining platform validation |
|---|---|---|
| macOS core | 22/22 tests in a Debug build on 2026-11-24, including PGN import/export, GIF encoder and Lichess protocol/client checks; last ASan/UBSan run 17/17 on 2026-10-03, including a stale app-cache regression | Broader compiler/architecture coverage |
| macOS application | Last recorded app build: 23/23 C/C++ tests with ASan/UBSan on 2026-10-03; native smoke and 113 animation scenarios included. Current Release app build: 29/29 tests on 2026-11-24 (no sanitizers); the native smoke covers the Lichess flow with injected server replies (lobby, resuming and leaving games, challenges, draws, endings), PGN import, GIF export and its progress dialog, click-to-open menus, both boards with the move panel, coordinates and captured pieces, the rotating 2D board (manual, per player to move, and on Lichess as Black, with mirrored animations), finished games kept on the board, the full-screen toggle and the optional idle animations, and saves the README screenshots. Actual-host serial checks are retained in the reverse workspace | Manual desktop/file-picker/audio checks, full screen and Retina scaling on real displays, and full original-runtime parity |
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
6. **Verify original and physical modem interoperability.** Capture move, board, promotion, chat, retry and quit exchanges with an original peer; then test physical dialing/flow control separately. A native pseudo-terminal pass cannot replace either result.
7. **Add native network play.** Multiplayer currently means two humans on one machine (Settings → Human White and Human Black) or the recovered modem session over a POSIX serial device (`--serial DEVICE`); there is no internet option. Two routes are planned:
   - *Serial-over-TCP bridge (no code change):* expose a pseudo-terminal per player with `socat` (for example `socat pty,link=/tmp/bc-serial,raw,echo=0 tcp-listen:PORT` on one host and `... tcp:HOST:PORT` on the other) and pass it to `--serial`. Untested: Dial Number and Set Auto Answer send Hayes `AT` commands, which reach the peer application instead of a modem, so session start-up must be checked with two instances before documenting the commands.
   - *Built-in TCP transport:* add native options such as `--host PORT` and `--connect HOST:PORT` that carry the existing recovered packets, checksums and retry logic over a socket and skip the `AT` dialing layer. Keep it a separate native adapter so the recovered modem session stays unchanged.

   Completion requires a full game between two networked instances covering moves, board setup, promotion, chat and quit on both sides, plus a documented behavior for dropped connections. It does not replace the original-peer comparison in milestone 6.
8. **Port the application to iPadOS.** SDL3 supports iOS, the renderer already scales the 511×320 content by whole pixels at full display density, and SDL delivers touches as mouse events, so board input and the click-to-open menus need no new input model. Required native adaptations:
   - *SDL3:* the Homebrew package is macOS-only; build SDL3 for iOS from source and select it with `CMAKE_PREFIX_PATH`.
   - *Build:* add an iOS configuration (`CMAKE_SYSTEM_NAME=iOS`, Xcode generator) producing an app bundle with `Info.plist` and icon.
   - *Data:* `--data` is unavailable; copy the external pack (about 69 MB) into the locally built bundle and resolve it from `SDL_GetBasePath()`. The pack stays outside the repository as today.
   - *Cursors:* `loadCursor` requires `SDL_CreateCursor` to succeed; skip the original cursors where the platform has no pointer cursor.
   - *Files:* SDL3's native file dialogs are believed unavailable on iOS (verify); save, open, PGN import/export and GIF export need the app's Documents folder (exposed in the Files app) or a native document picker.
   - *Modem:* no serial devices exist on iPadOS; hide modem play until the TCP transport from milestone 7 is available.
   - *Shortcuts:* Command shortcuts work only with a hardware keyboard; every action remains reachable from the menus.
   - *Layout:* the both-boards canvas (882×478) is too wide for portrait; use the stacked layout from milestone 11 there.

   The bundle embeds original resources, so builds are for personal installation from Xcode only (free Apple ID provisioning expires after 7 days; a paid developer account lasts one year), never App Store or TestFlight distribution. Completion requires a full game on a physical iPad or the Simulator covering board input, menus, promotion, setup, audio and save/load, with the run recorded in the validation table.

9. **Validate and finish Lichess play.** Stages 1–3 of the plan and most of stage 4 are implemented (see the Lichess row above and the README). API facts follow the published specification in [lichess-org/api](https://github.com/lichess-org/api) (`doc/specs`, checked 2026-11-24): Board API games are Rapid, Classical or Correspondence, with Blitz also allowed for direct challenges and the Lichess AI; one request at a time, back off a minute after HTTP 429; game clocks are milliseconds, challenge limits seconds and seek times minutes; `initialFen` is `"startpos"` for normal games and castling may arrive king-to-rook (`e1h1`). Remaining:
    - *Real-account validation:* a full casual game against another account covering normal moves, castling, en passant, promotion, resignation and a timeout; a game against the Lichess AI; an analysis import; a reconnect in the middle of a game.
    - *Playing Black:* done. The board turns to Black's side while playing Black; the 3D board turns too with Settings → Rotate 3D Board Too. The 3D artwork is drawn from White's side only, so the turned 3D board plans each move on the side-swapped position, where every piece already faces the right way, and recolors the sprites back to their real side.
    - *Stage 4 leftovers:* chat (reusing the recovered Talk dialog), takeback offers, claim victory/draw when `opponentGone` allows it.
    - *Platform:* libsecret token storage on Linux; OAuth PKCE login in place of pasted tokens; optional rated games.
    - *Architecture note:* `LichessClient` (libcurl, worker thread plus one thread per stream) is polled from the SDL loop, so all game state stays on the main thread; `LichessProtocol` holds the pure, tested translation logic.

    Completion requires the real-account runs above recorded in the validation table. Network play from milestone 7 stays independent of Lichess.

10. **Polish GIF export.** The current export is complete but minimal.
    - *Responsiveness:* render frames incrementally from the event loop with a Cancel button, instead of blocking input behind the progress dialog until the file is written.
    - *Failure cleanup:* write to a temporary file and rename on success, so an interrupted or failed export never leaves a partial GIF (PGN and save files already use atomic writes).
    - *Options:* offer export speed (reusing the Animation Speed choices) and scale (1x/2x), independent of the live settings; the export stays on the 3D board.

    Completion requires a native check that cancels an export mid-run, confirms no file remains and the live state is unchanged, and a manual long-game export with the window kept responsive.

11. **Refine the screen layout.** Both boards with the move panel, whole-pixel full screen, board coordinates, the captured-pieces strip (half-size original sprites, history-based, en passant included and promotions excluded) and a rotatable 2D board (pieces, hit testing, hints, coordinates, and animations mirrored anchor to anchor) are in place; coordinates and the captured strip are on by default and can be switched off to restore the original single view. Remaining ideas:
    - *Stacked layout:* place the flat board and panel under the 3D board when the window is taller than wide (and on iPad in portrait).
    - *Larger 3D board:* at high whole-pixel scales, draw the 3D board one step larger than the flat board, since it carries the animations.
    - *Full-screen backdrop:* confirm the color around the canvas in full screen on macOS and Linux, and choose a Mac-desktop gray or black deliberately.

    Completion requires smoke checks for each layout's hit testing and panel contents, plus manual screenshots at 1x, 2x and full screen on a Retina and a standard display.

These are completion criteria, not release dates. New translations should retain original function addresses, explain their purpose and assumptions, and add focused checks. Keep fresh builds, saved-output inspection, native execution, original comparisons and physical-device results distinct.

## Project boundaries

The repository supports user-supplied data; it does not distribute original game/system resources or extraction tooling. The separate reverse workspace retains the broad Ghidra export and its unresolved decompiler failures; completing that export is a different task from validating a native feature.

This is not a clean-room implementation. The [MIT license](../LICENSE) covers project contributions within the scope described in [NOTICE](../NOTICE), and does not grant rights to original or third-party material.
