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

Linux:

```bash
QT_CMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64/lib/cmake ./scripts/build_and_run_linux.sh
```

Build + test only (skip running app):

```bash
./scripts/build_and_run_linux.sh --no-run
```

Windows (Command Prompt):

```bat
set QT_CMAKE_PREFIX_PATH=%USERPROFILE%\Qt\6.11.0\msvc2022_64\lib\cmake
scripts\build_and_run.bat
```

Build + test only (skip running app):

```bat
scripts\build_and_run.bat --no-run
```

## macOS Package (DMG)

```bash
./scripts/package_macos.sh
```

The script tries to generate `DMG` first and always generates a `.tar.gz` fallback package.
Output files are generated under `build-qt/packages/`.

## Linux Package (DEB/RPM/TGZ)

```bash
QT_CMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64/lib/cmake ./scripts/package_linux.sh
```

Skip tests:

```bash
./scripts/package_linux.sh --skip-tests
```

The script tries `DEB` and `RPM` when required tools exist, and always generates a `.tar.gz` fallback package.
Output files are generated under `build-qt/packages/`.

## Windows Package (NSIS/ZIP)

```bat
set QT_CMAKE_PREFIX_PATH=%USERPROFILE%\Qt\6.11.0\msvc2022_64\lib\cmake
scripts\package_windows.bat
```

Skip tests:

```bat
scripts\package_windows.bat --skip-tests
```

The script tries `NSIS` when `makensis` is available, and always generates a `.zip` fallback package.
Output files are generated under `build-qt\packages\`.

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
