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

set "RUN_APP=1"
if /I "%~1"=="--no-run" set "RUN_APP=0"

if defined QT_CMAKE_PREFIX_PATH (
  set "CMAKE_PREFIX_PATH=%QT_CMAKE_PREFIX_PATH%"
) else (
  set "CMAKE_PREFIX_PATH="
  for /d %%D in ("%USERPROFILE%\Qt\*") do (
    for %%K in (msvc2022_64 msvc2019_64 mingw_64) do (
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
  echo   scripts\build_and_run.bat
  exit /b 1
)

echo [1/4] Configure
cmake -S "%ROOT_DIR%" ^
  -B "%BUILD_DIR%" ^
  -DCMAKE_PREFIX_PATH="%CMAKE_PREFIX_PATH%" ^
  -DCMAKE_BUILD_TYPE="%BUILD_TYPE%"
if errorlevel 1 exit /b 1

if defined NUMBER_OF_PROCESSORS (
  set "CORES=%NUMBER_OF_PROCESSORS%"
) else (
  set "CORES=8"
)

echo [2/4] Build (jobs=!CORES!)
cmake --build "%BUILD_DIR%" --config "%BUILD_TYPE%" --parallel !CORES!
if errorlevel 1 exit /b 1

echo [3/4] Test
ctest --test-dir "%BUILD_DIR%" --output-on-failure -C "%BUILD_TYPE%"
if errorlevel 1 exit /b 1

if "%RUN_APP%"=="0" (
  echo [4/4] Skip run (--no-run)
  exit /b 0
)

echo [4/4] Run
set "APP_EXE=%BUILD_DIR%\src\%BUILD_TYPE%\massiveedit.exe"
if exist "%APP_EXE%" (
  "%APP_EXE%"
  exit /b %errorlevel%
)

set "APP_EXE=%BUILD_DIR%\src\massiveedit.exe"
if exist "%APP_EXE%" (
  "%APP_EXE%"
  exit /b %errorlevel%
)

echo Failed to find built executable.
echo Checked:
echo   "%BUILD_DIR%\src\%BUILD_TYPE%\massiveedit.exe"
echo   "%BUILD_DIR%\src\massiveedit.exe"
exit /b 1

