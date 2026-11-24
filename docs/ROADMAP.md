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
| Rendering | Perspective and flat boards, original shape selection, anchors, hit testing, layering and two-bit side recoloring | Initial scene has a scoped original capture comparison; full UI parity remains open |
| Movement and combat | Recovered movement/combat graphs, turns, knight obstacles, rook transformations, fades, castling/en-passant submoves, promotion order and checkmate capture; native Settings → Animation Speed from 1x (original) to 2x | 113 native animation scenarios pass; original timing/audio/choreography comparison remains open |
| Animation data | 646 recovered sequences and 6,824 cumulative frames supported by the external pack | Saved pixels and regenerated data were checked; asset recovery does not prove every live call path |
| Menus and dialogs | Macintosh-style menu tracking, recovered alerts/dialog resources, About rendering, player/view settings and button-release handling | Native resource/UI checks pass; complete window, menu, pressed and disabled appearance needs original captures |
| File handling | Open, Save and Save As; recovered 78-byte format; opaque-byte preservation; atomic writes; macOS `Game`/`IPBC` Finder identity | Native format and transfer checks pass; original-application exchange is unverified; interactive OS file pickers need manual coverage |
| PGN and GIF | Native File menu extensions: PGN export (SAN, FEN start, result), PGN import (first game, FEN start including en passant, comments/variations/annotations skipped, SAN and long algebraic moves resolved against legal moves) and animated GIF export that replays the session offscreen through the recovered animations at twice the original speed, merging identical frames and storing changed rectangles | Core encoder/decoder round trip and PGN import/export checks; encoder output (LZW table resets, odd sizes, merged frames, sub-rectangles) decoded pixel-exact by ffmpeg and ImageMagick on 2026-11-24; native smoke imports a mated game, exports a 3D GIF and verifies the RNG stream, session and animation state are unchanged. GIF export blocks input while it runs, and a render failure mid-export leaves a partial file |
| Board setup | Piece placement/removal, Clear Board, position validation and acceptance/cancel paths | Core and native setup checks pass |
| Audio | Recovered sound events and effect selection with SDL playback and pitch adjustment | Dummy-audio tests do not verify audible output or original sound timing |
| Modem play | Recovered packets/checksums, retry/session logic, board/move/promotion/chat/quit paths and POSIX serial hosting | Native pseudo-terminal checks pass; original peer and physical modem compatibility are unverified |
| Idle animations | Twelve recovered bishop scripts / 218 frames, with duplicate entries and a bounded caller audit | Data recovery only: no proven selection/time gate; automatic idle playback is not implemented |

Original graphics, sound, fonts, resource tables, archives and disk images are not shipped here. The [data contract](DATA.md) describes the external inputs.

## Validation status

| Target | Recorded result | Remaining platform validation |
|---|---|---|
| macOS core | 20/20 tests in a Debug build on 2026-11-24, including PGN import/export and GIF encoder checks; last ASan/UBSan run 17/17 on 2026-10-03, including a stale app-cache regression | Broader compiler/architecture coverage |
| macOS application | Last recorded app build: 23/23 C/C++ tests with ASan/UBSan on 2026-10-03; native smoke and 113 animation scenarios included. The current app build registers 27 tests, including the PGN import/GIF export smoke, and has not yet been rerun against a data pack. Actual-host serial checks are retained in the reverse workspace | Manual desktop/file-picker/audio checks and full original-runtime parity |
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
9. **Port the application to iPadOS.** SDL3 supports iOS, the renderer already scales the 511×320 content with logical presentation, and SDL delivers touches as mouse events, so board input and press-drag-release menus need no new input model. Required native adaptations:
   - *SDL3:* the Homebrew package is macOS-only; build SDL3 for iOS from source and select it with `CMAKE_PREFIX_PATH`.
   - *Build:* add an iOS configuration (`CMAKE_SYSTEM_NAME=iOS`, Xcode generator) producing an app bundle with `Info.plist` and icon.
   - *Data:* `--data` is unavailable; copy the external pack (about 69 MB) into the locally built bundle and resolve it from `SDL_GetBasePath()`. The pack stays outside the repository as today.
   - *Cursors:* `loadCursor` requires `SDL_CreateCursor` to succeed; skip the original cursors where the platform has no pointer cursor.
   - *Files:* SDL3's native file dialogs are believed unavailable on iOS (verify); save, open, PGN import/export and GIF export need the app's Documents folder (exposed in the Files app) or a native document picker.
   - *Modem:* no serial devices exist on iPadOS; hide modem play until the TCP transport from milestone 8 is available.
   - *Shortcuts:* Command shortcuts work only with a hardware keyboard; every action remains reachable from the menus.

   The bundle embeds original resources, so builds are for personal installation from Xcode only (free Apple ID provisioning expires after 7 days; a paid developer account lasts one year), never App Store or TestFlight distribution. Completion requires a full game on a physical iPad or the Simulator covering board input, menus, promotion, setup, audio and save/load, with the run recorded in the validation table.
10. **Show both boards side by side.** Add an opt-in native view (Settings → Show Both Boards and a `--both` flag) with the perspective board on the left and the flat board on the right; the single original view stays the default so existing screenshots and animation checks remain valid.
    - *Layout:* widen the logical content from 511×320 to about 1022×320, draw the flat board through the existing render path offset by 511 points, and keep the menu bar across the full width. The window opens at double scale and logical presentation shrinks it on smaller displays.
    - *Input:* accept clicks on either board, mapping the right half back through `original_hit_square(x - 511, y, true)`; show selections and hint outlines on both.
    - *Animation:* recovered movement and combat play on the perspective board only. The flat board keeps the pre-move position until the animation completes, so both views always agree; a second simultaneous animation host (and duplicated sound) is out of scope.
    - *Dialogs and setup:* alerts, promotion and Set Time stay over the perspective board. Set Up Board already forces the flat view and uses both content edges for its palettes, so it temporarily returns to the single flat view.

    Completion requires a focused check that clicks on both halves, confirms the flat board stays on the pre-move position during an animation and matches afterwards, and verifies the default single view is unchanged.
11. **Integrate Lichess.** Add optional online play and analysis through the public [Lichess API](https://lichess.org/api) as a native extension, separate from the recovered engine and modem code.
    - *Analysis (smallest step):* upload the current game's PGN (from File → Export PGN) through the Lichess import endpoint and open the returned analysis URL in the browser. No account is needed.
    - *Online play:* use the Board API for the user's own human account: authenticate with a personal OAuth token, stream incoming game events, send moves in UCI notation and present opponent moves through the same animation path as modem moves. Seeks, challenges, resign, draw offers and aborts map to new menu items. Lichess board-API rules (permitted time controls, rate limits, token scopes) must be checked against the current documentation before implementation.
    - *Fair play:* while a Lichess game is active, disable Suggest Move, Force Move and every Mac-controlled side; the recovered engine must never move or advise for the user's account.
    - *Clocks and results:* the original interface has no clock, so display both players' remaining time, and handle flagging, disconnects, abort and server-declared results.
    - *Platform:* requires a native HTTPS/TLS client with streaming (libcurl or platform APIs) and secure token storage (macOS Keychain); no credential is written to save files or logs.

    Completion requires a full casual game against another account covering normal moves, castling, en passant, promotion, resignation and a timeout, plus a successful analysis import. Network play from milestone 8 stays independent of Lichess.
12. **Polish GIF export.** The current export is complete but minimal.
    - *Responsiveness:* render frames incrementally from the event loop with a progress indicator and Cancel, instead of blocking input until the file is written.
    - *Failure cleanup:* write to a temporary file and rename on success, so an interrupted or failed export never leaves a partial GIF (PGN and save files already use atomic writes).
    - *Options:* offer export speed (reusing the Animation Speed choices), scale (1x/2x) and the board view, independent of the live settings.

    Completion requires a native check that cancels an export mid-run, confirms no file remains and the live state is unchanged, and a manual long-game export with the window kept responsive.

These are completion criteria, not release dates. New translations should retain original function addresses, explain their purpose and assumptions, and add focused checks. Keep fresh builds, saved-output inspection, native execution, original comparisons and physical-device results distinct.

## Project boundaries

The repository supports user-supplied data; it does not distribute original game/system resources or extraction tooling. The separate reverse workspace retains the broad Ghidra export and its unresolved decompiler failures; completing that export is a different task from validating a native feature.

This is not a clean-room implementation. The [MIT license](../LICENSE) covers project contributions within the scope described in [NOTICE](../NOTICE), and does not grant rights to original or third-party material.
