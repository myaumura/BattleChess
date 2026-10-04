# BattleChess code style

BattleChess combines a C11 engine with a C++17/SDL3 host. Use the conventions of the layer being changed and keep refactors focused on the affected module.

## Code areas

| Area | Role | Naming approach |
|---|---|---|
| `lib/*.c` and their headers | Recovered engine rules, search, saves, setup and animation | Existing C conventions and recovered names |
| `src/` | Application, SDL rendering and native platform adapters | C++ conventions below for new and refactored modules |
| `lib/*.cpp` | C++ presentation and rendering helpers | C++ conventions for native helpers; preserve existing recovery interfaces |
| `tests/` | Core checks and native integration checks | Follow the language and module being checked |
| External `BC_DATA_DIR` pack | Generated resource catalogs, fonts and original data | Generator-owned names and formatting; see [the data contract](DATA.md) |

Preserve original offsets, recovered labels and provenance comments. Keep native adapters identified as such. Style changes must preserve board representation, resource IDs, error messages, rendering/input order, timing and RNG usage.

## C++ host naming

| Element | Style | Examples |
|---|---|---|
| Types, including structs, classes and enums | PascalCase | `GameSession`, `SetupUI`, `ResourceDialogKind` |
| Fields, methods and C++ free functions | lowerCamelCase | `heldPiece`, `restore`, `findResourceDialog` |
| Parameters and local variables | lowerCamelCase | `engineSquare`, `itemIndex` |
| `enum class` entries | lowerCamelCase | `dialog`, `alert`, `localMove` |
| Macros and header guards | SCREAMING_SNAKE_CASE | `BC_SETUP_UI_H`, `BC_RESOURCE_DIALOG_H` |
| C++ constants (`constexpr` and namespace-scope `const`) | kCamelCase | `kMaxBoardPieces`, `kItemTypeMask` |
| Named namespaces and namespace aliases | lowerCamelCase | `fs`; `resourceDialog` if introduced |

Selected declarations from [ResourceDialog.h](../src/ResourceDialog.h) and [SetupUI.h](../src/SetupUI.h):

```cpp
enum class ResourceDialogKind : uint8_t {
    dialog = 0,
    alert = 1,
};

struct SetupUI {
    Position originalPosition{};
    Position position{};
    uint8_t heldPiece = 0;
    uint8_t heldSide = 0;

    void begin(const Position &initialPosition);
    void restore();
    const char *done(BCGame &game);
};
```

Calls into the C engine and SDL retain their interface names: `bc_setup_validate`, `reset_board` and `SDL_RenderRect`. C-owned fields such as `BCGame::history_count` and generated fields such as `OriginalDialogItem::resource_id` retain their spelling when used from C++.

## C engine naming

Follow the existing C module: native public functions use the `bc_` prefix and snake_case (`bc_game_apply`, `bc_setup_commit`); fields, parameters and local variables use snake_case (`history_count`, `undo_snapshot`). Existing types include `BCGame`, `BCMove` and `Position`. Recovered routine names such as `calculate_piece_lists` remain intact.

C macros and enum constants use SCREAMING_SNAKE_CASE, including module prefixes where established (`BC_GAME_MOVE_CAPACITY`, `BC_GAME_HISTORY_CAPACITY`). The C++ `kCamelCase` and `enum class` rules apply to C++ declarations.

## Filenames

New C++ headers, implementations and checks use PascalCase: `ResourceDialog.h`, `ResourceDialog.cpp` and `SetupUICheck.cpp`. Keep initialisms uppercase, as in `SDLHelpers.cpp` and `SetupUI.h`. The conventional entry point remains `main.cpp`.

C modules and checks follow their existing snake_case pairs: `setup_board.h`, `setup_board.c` and `setup_board_check.c`. Existing C++ filenames retain their names until that module is refactored. Generated headers such as `original.hpp` retain the filenames required by the external data contract.

When renaming a file, update every include and CMake source path. Their case must match the filename on disk, including on case-sensitive systems.

## Formatting and namespaces

[config/clang-format.yaml](../config/clang-format.yaml) is the formatter configuration. It uses LLVM style with four-space indentation, a 100-column limit and opening braces on the same line. Functions, conditionals and loops use multiline bodies. Include sorting is disabled.

Indent the contents of every namespace, including anonymous namespaces, by four spaces (`NamespaceIndentation: All`). Keep implementation helpers and constants in anonymous namespaces inside `.cpp` files, as in [ResourceDialog.cpp](../src/ResourceDialog.cpp) and [SetupUI.cpp](../src/SetupUI.cpp). A named module namespace can group public declarations and definitions; update its callers together when introducing one.

Example from `SetupUI.cpp`:

```cpp
namespace {
    constexpr unsigned kMaxBoardPieces = 32;

    bool isBoardSquare(int square) {
        return square >= 0 && square < 120 && !(square & 0x88);
    }
} // namespace
```

Run from the repository root with `clang-format` installed. If the executable is outside `PATH`, use its full path. Pass the configuration explicitly:

```sh
clang-format --style=file:config/clang-format.yaml -i \
    src/ResourceDialog.h src/ResourceDialog.cpp
```

To check formatting without changing files:

```sh
clang-format --style=file:config/clang-format.yaml --dry-run --Werror \
    src/ResourceDialog.h src/ResourceDialog.cpp
```

Format only files involved in the change. Recovered and generated source keeps its separate provenance and generation workflow; see [the data contract](DATA.md).

## Readability and checks

Keep public headers small and group related work into named functions. For example, `drawResourceDialog` coordinates frame, icon and item drawing while private helpers handle text wrapping, pictures and clipping. Preserve the sequence of operations and failure handling when extracting those helpers.

Name C++ check scenarios after the behavior they verify, such as `checkClearPreservesHeldPiece` and `checkDoneRejectsInvalidBoard` in [SetupUICheck.cpp](../tests/SetupUICheck.cpp). Preserve existing assertions and validate changes with [the documented build checks](BUILD.md#checks-and-troubleshooting).
