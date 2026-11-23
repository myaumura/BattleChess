# Roadmap and feature coverage

Status recorded **2026-10-03**. The project targets the 1991 Macintosh release of Battle Chess for Motorola 68k processors, with a C11 engine and a C++17/SDL3 host for modern systems. It is playable with an external prepared data pack. Native test coverage and equivalence to the original game are separate milestones.

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
| Movement and combat | Recovered movement/combat graphs, turns, knight obstacles, rook transformations, fades, castling/en-passant submoves, promotion order and checkmate capture | 113 native animation scenarios pass; original timing/audio/choreography comparison remains open |
| Animation data | 646 recovered sequences and 6,824 cumulative frames supported by the external pack | Saved pixels and regenerated data were checked; asset recovery does not prove every live call path |
| Menus and dialogs | Macintosh-style menu tracking, recovered alerts/dialog resources, About rendering, player/view settings and button-release handling | Native resource/UI checks pass; complete window, menu, pressed and disabled appearance needs original captures |
| File handling | Open, Save and Save As; recovered 78-byte format; opaque-byte preservation; atomic writes; macOS `Game`/`IPBC` Finder identity | Native format and transfer checks pass; original-application exchange is unverified; interactive OS file pickers need manual coverage |
| Board setup | Piece placement/removal, Clear Board, position validation and acceptance/cancel paths | Core and native setup checks pass |
| Audio | Recovered sound events and effect selection with SDL playback and pitch adjustment | Dummy-audio tests do not verify audible output or original sound timing |
| Modem play | Recovered packets/checksums, retry/session logic, board/move/promotion/chat/quit paths and POSIX serial hosting | Native pseudo-terminal checks pass; original peer and physical modem compatibility are unverified |
| Idle animations | Twelve recovered bishop scripts / 218 frames, with duplicate entries and a bounded caller audit | Data recovery only: no proven selection/time gate; automatic idle playback is not implemented |

Original graphics, sound, fonts, resource tables, archives and disk images are not shipped here. The [data contract](DATA.md) describes the external inputs.

## Validation status

| Target | Recorded result | Remaining platform validation |
|---|---|---|
| macOS core | Current source-only isolation build: 17/17 tests with ASan/UBSan on 2026-10-03, including a stale app-cache regression | Broader compiler/architecture coverage |
| macOS application | Current app build: 23/23 C/C++ tests with ASan/UBSan on 2026-10-03; native smoke and 113 animation scenarios included. Actual-host serial checks are retained in the reverse workspace | Manual desktop/file-picker/audio checks and full original-runtime parity |
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
   - *Files:* SDL3's native file dialogs are believed unavailable on iOS (verify); save, open and PGN export need the app's Documents folder (exposed in the Files app) or a native document picker.
   - *Modem:* no serial devices exist on iPadOS; hide modem play until the TCP transport from milestone 8 is available.
   - *Shortcuts:* Command shortcuts work only with a hardware keyboard; every action remains reachable from the menus.

   The bundle embeds original resources, so builds are for personal installation from Xcode only (free Apple ID provisioning expires after 7 days; a paid developer account lasts one year), never App Store or TestFlight distribution. Completion requires a full game on a physical iPad or the Simulator covering board input, menus, promotion, setup, audio and save/load, with the run recorded in the validation table.
10. **Show both boards side by side.** Add an opt-in native view (Settings → Show Both Boards and a `--both` flag) with the perspective board on the left and the flat board on the right; the single original view stays the default so existing screenshots and animation checks remain valid.
    - *Layout:* widen the logical content from 511×320 to about 1022×320, draw the flat board through the existing render path offset by 511 points, and keep the menu bar across the full width. The window opens at double scale and logical presentation shrinks it on smaller displays.
    - *Input:* accept clicks on either board, mapping the right half back through `original_hit_square(x - 511, y, true)`; show selections and hint outlines on both.
    - *Animation:* recovered movement and combat play on the perspective board only. The flat board keeps the pre-move position until the animation completes, so both views always agree; a second simultaneous animation host (and duplicated sound) is out of scope.
    - *Dialogs and setup:* alerts, promotion and Set Time stay over the perspective board. Set Up Board already forces the flat view and uses both content edges for its palettes, so it temporarily returns to the single flat view.

    Completion requires a focused check that clicks on both halves, confirms the flat board stays on the pre-move position during an animation and matches afterwards, and verifies the default single view is unchanged.
11. **Integrate Lichess.** Add optional online play and analysis through the public [Lichess API](https://lichess.org/api) as a native extension, separate from the recovered engine and modem code. API facts below come from the published specification in [lichess-org/api](https://github.com/lichess-org/api) (`doc/specs`); recheck them before implementation.
    - *Board API rules:* works with normal Lichess accounts and the `board:play` token scope (challenges also need the challenge scopes). Engine assistance or any outside help is forbidden. Time controls are Rapid, Classical and Correspondence; Blitz is also possible for direct challenges, games against the Lichess AI and bulk pairing. Bullet is unavailable. Only one request at a time; HTTP 429 means a rate limit was hit, so back off. Send a `User-Agent`.
    - *Endpoints:* incoming events `GET /api/stream/event` (one global stream per token; opening another closes the previous one); public seek `POST /api/board/seek` (a real-time seek stays active only while its connection is open; open the event stream first); game state `GET /api/board/game/stream/{gameId}` (first line `gameFull`, then `gameState`, `chatLine`, `opponentGone`; closed when the game ends); moves `POST /api/board/game/{gameId}/move/{move}` in UCI with an optional draw offer; plus abort, resign, chat, claim-draw, claim-victory, draw and takeback endpoints under `/api/board/game/{gameId}/`. Challenges use `POST /api/challenge/{username}` and `POST /api/challenge/ai`; PGN import uses `POST /api/import` (100 games per hour anonymously, 200 with OAuth). Clocks `wtime`/`btime` are milliseconds.
    - *Interface:* a Lichess menu in the slot used by Modem when no serial device is connected: Connect…, Play the Lichess AI…, Challenge Player…, Find Opponent (Rapid/Classical), Offer Draw, Resign/Abort, Chat… (reusing the recovered Talk dialog) and Disconnect. Incoming challenges appear as Accept/Decline alerts. Opponent moves play through the recovered movement and combat animations. Both clocks are drawn in the free right side of the menu bar.
    - *Fair play:* while a Lichess game is active, disable Suggest Move, Force Move, hint outlines and every Mac-controlled side; the recovered engine must never move or advise for the user's account.
    - *Architecture:* a `LichessHost` adapter in the style of `ModemHost`. Background threads hold the event, game and seek streams and post results to the SDL event queue (as the file dialog does), so all game state changes stay on the main thread; ordinary requests go through a serialized queue. Requires an HTTPS client with streaming (libcurl ships with macOS and common Linux distributions) and a small JSON parser (for example one vendored single header); the project currently depends only on SDL3.
    - *Move translation and sync:* send UCI built from `BCMove` (`e2e4`, `e7e8q`). Match incoming UCI against `bcGameLegalMoves` as the PGN exporter does, accepting both `e1g1` and king-to-rook `e1h1` castling. On `gameFull`, rebuild the session from the initial position and move list without animation (this also handles reconnects); animate only new moves from `gameState`. Any mismatch triggers a full resync instead of a guess.
    - *Token storage:* paste a personal access token once and keep it in the macOS Keychain (libsecret on Linux), never in save files or logs. OAuth PKCE login can replace token pasting later.
    - *Known constraints:* the board is always drawn from White's side, so playing Black needs a flipped view (at least for the flat board). Combat animations take seconds of clock time; online play should default to faster animation or allow skipping combat.
    - *Order:* (1) Analyse on Lichess: upload the current PGN through `/api/import` and open the result in the browser; HTTP only, no account or streaming. (2) Play the Lichess AI: first real game with streaming, sync and clocks, no opponent waiting. (3) Challenge players by username and accept incoming challenges. (4) Public seeks, draw offers, takebacks, chat and `opponentGone`/claim victory.
    - *Testing:* a local fake server replaying recorded NDJSON streams so CI covers sync, reconnects, endings and illegal-move resync without an account; final validation uses real accounts.

    Completion requires a full casual game against another account covering normal moves, castling, en passant, promotion, resignation and a timeout, a game against the Lichess AI, plus a successful analysis import. Network play from milestone 8 stays independent of Lichess.

These are completion criteria, not release dates. New translations should retain original function addresses, explain their purpose and assumptions, and add focused checks. Keep fresh builds, saved-output inspection, native execution, original comparisons and physical-device results distinct.

## Project boundaries

The repository supports user-supplied data; it does not distribute original game/system resources or extraction tooling. The separate reverse workspace retains the broad Ghidra export and its unresolved decompiler failures; completing that export is a different task from validating a native feature.

This is not a clean-room implementation. The [MIT license](../LICENSE) covers project contributions within the scope described in [NOTICE](../NOTICE), and does not grant rights to original or third-party material.
