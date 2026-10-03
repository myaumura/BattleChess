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

These are completion criteria, not release dates. New translations should retain original function addresses, explain their purpose and assumptions, and add focused checks. Keep fresh builds, saved-output inspection, native execution, original comparisons and physical-device results distinct.

## Project boundaries

The repository supports user-supplied data; it does not distribute original game/system resources or extraction tooling. The separate reverse workspace retains the broad Ghidra export and its unresolved decompiler failures; completing that export is a different task from validating a native feature.

This is not a clean-room implementation. The [MIT license](../LICENSE) covers project contributions within the scope described in [NOTICE](../NOTICE), and does not grant rights to original or third-party material.
