@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "ROOT_DIR=%%~fI"

if not defined BUILD_DIR set "BUILD_DIR=%ROOT_DIR%\build-qt"
if defined CMAKE_BUILD_TYPE (
  set "BUILD_TYPE=%CMAKE_BUILD_TYPE%"
) else (
  set "BUILD_TYPE=Release"
)

if /I not "%OS%"=="Windows_NT" (
  echo This script only supports Windows.
  exit /b 1
)

if defined QT_CMAKE_PREFIX_PATH (
  set "CMAKE_PREFIX_PATH=%QT_CMAKE_PREFIX_PATH%"
) else (
  set "CMAKE_PREFIX_PATH="
  for /d %%D in ("%USERPROFILE%\Qt\*") do (
    for %%K in (msvc2022_arm64 msvc2022_64 msvc2019_64 mingw_64) do (
      if exist "%%~fD\%%K\lib\cmake\Qt6\Qt6Config.cmake" (
        set "CMAKE_PREFIX_PATH=%%~fD\%%K\lib\cmake"
      )
    )
  )
)

if not defined CMAKE_PREFIX_PATH (
  echo Failed to find Qt CMake path.
  echo Set QT_CMAKE_PREFIX_PATH, for example:
  echo   set QT_CMAKE_PREFIX_PATH=%%USERPROFILE%%\Qt\6.11.0\msvc2022_64\lib\cmake
  echo   scripts\run_windows.bat
  exit /b 1
)

set "QT_BIN_DIR="
for %%I in ("%CMAKE_PREFIX_PATH%\..\..") do set "QT_BIN_DIR=%%~fI\bin"
if exist "%QT_BIN_DIR%\Qt6Core.dll" (
  set "PATH=%QT_BIN_DIR%;%PATH%"
  echo Using Qt runtime bin: %QT_BIN_DIR%
) else (
  echo Warning: Qt runtime bin not found from CMAKE_PREFIX_PATH.
  echo   Expected: %QT_BIN_DIR%\Qt6Core.dll
)

set "APP_EXE=%BUILD_DIR%\src\%BUILD_TYPE%\massiveedit.exe"
if exist "%APP_EXE%" (
  echo Launch: %APP_EXE%
  start "" "%APP_EXE%"
  exit /b 0
)

set "APP_EXE=%BUILD_DIR%\src\massiveedit.exe"
if exist "%APP_EXE%" (
  echo Launch: %APP_EXE%
  start "" "%APP_EXE%"
  exit /b 0
)

set "APP_EXE=%BUILD_DIR%\Release\massiveedit.exe"
if exist "%APP_EXE%" (
  echo Launch: %APP_EXE%
  start "" "%APP_EXE%"
  exit /b 0
)

echo Failed to find built executable.
echo Checked:
echo   "%BUILD_DIR%\src\%BUILD_TYPE%\massiveedit.exe"
echo   "%BUILD_DIR%\src\massiveedit.exe"
echo   "%BUILD_DIR%\Release\massiveedit.exe"
exit /b 1

