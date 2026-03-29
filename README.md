# MassiveEdit

MassiveEdit is a cross-platform large-file viewer/editor prototype inspired by tools like EmEditor.

## Goals

- Open very large text files quickly.
- Support progressive indexing and virtualized rendering.
- Provide reliable editing primitives that can evolve into a true streaming architecture.

## Tech Stack

- C++20
- Qt 6 (Core, Gui, Widgets)
- CMake

## Build

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/qt
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Or use one-command local build + run:

```bash
./scripts/build_and_run.sh
```

## macOS Package (DMG)

```bash
./scripts/package_macos.sh
```

The script tries to generate `DMG` first and always generates a `.tar.gz` fallback package.
Output files are generated under `build-qt/packages/`.

## Current Status

- Project skeleton is in place.
- UI shell can open large files and render line windows.
- Core now uses streaming reads through chunk cache + file-backed piece table.
- Line indexing is lazy and incremental.
- Undo/redo transactional edit log is wired.
- Background search runs on a worker pool and reports async completion.

## Directory Layout

```text
include/massiveedit/
  core/
    file/
  ui/
src/
  core/
  ui/
tests/
docs/
```

See `docs/architecture.md` for module boundaries and next milestones.
