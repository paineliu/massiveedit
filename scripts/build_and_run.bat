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
  echo   set QT_CMAKE_PREFIX_PATH=%%USERPROFILE%%\Qt\6.11.0\msvc2022_arm64\lib\cmake
  echo   scripts\build_and_run.bat
  exit /b 1
)

if defined CMAKE_GENERATOR (
  set "CMAKE_GENERATOR_NAME=%CMAKE_GENERATOR%"
) else (
  set "CMAKE_GENERATOR_NAME=Visual Studio 17 2022"
)

if defined CMAKE_GENERATOR_PLATFORM (
  set "CMAKE_GENERATOR_PLATFORM_NAME=%CMAKE_GENERATOR_PLATFORM%"
) else (
  echo;%CMAKE_PREFIX_PATH% | findstr /I "arm64" >nul
  if not errorlevel 1 (
    set "CMAKE_GENERATOR_PLATFORM_NAME=ARM64"
  ) else (
    set "CMAKE_GENERATOR_PLATFORM_NAME=x64"
  )
)

set "IS_VS_GENERATOR=0"
echo;%CMAKE_GENERATOR_NAME% | findstr /I "Visual Studio" >nul
if not errorlevel 1 set "IS_VS_GENERATOR=1"

echo Using Qt CMake path: %CMAKE_PREFIX_PATH%
echo Using CMake generator: %CMAKE_GENERATOR_NAME%
if "%IS_VS_GENERATOR%"=="1" echo Using generator platform: %CMAKE_GENERATOR_PLATFORM_NAME%

echo [1/4] Configure
if "%IS_VS_GENERATOR%"=="1" (
  cmake -S "%ROOT_DIR%" ^
    -B "%BUILD_DIR%" ^
    -G "%CMAKE_GENERATOR_NAME%" ^
    -A "%CMAKE_GENERATOR_PLATFORM_NAME%" ^
    -DCMAKE_PREFIX_PATH="%CMAKE_PREFIX_PATH%" ^
    -DCMAKE_BUILD_TYPE="%BUILD_TYPE%"
) else (
  cmake -S "%ROOT_DIR%" ^
    -B "%BUILD_DIR%" ^
    -G "%CMAKE_GENERATOR_NAME%" ^
    -DCMAKE_PREFIX_PATH="%CMAKE_PREFIX_PATH%" ^
    -DCMAKE_BUILD_TYPE="%BUILD_TYPE%"
)
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
