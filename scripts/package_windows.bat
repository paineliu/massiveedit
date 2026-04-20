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

set "RUN_TESTS=1"
if /I "%~1"=="--skip-tests" set "RUN_TESTS=0"

if /I not "%OS%"=="Windows_NT" (
  echo This script only supports Windows.
  exit /b 1
)

if defined CMAKE_GENERATOR (
  set "CMAKE_GENERATOR_NAME=%CMAKE_GENERATOR%"
) else (
  set "CMAKE_GENERATOR_NAME=Visual Studio 17 2022"
)

set "IS_VS_GENERATOR=0"
echo;%CMAKE_GENERATOR_NAME% | findstr /I "Visual Studio" >nul
if not errorlevel 1 set "IS_VS_GENERATOR=1"

if defined QT_CMAKE_PREFIX_PATH (
  set "CMAKE_PREFIX_PATH=%QT_CMAKE_PREFIX_PATH%"
) else (
  set "CMAKE_PREFIX_PATH="
  if "%IS_VS_GENERATOR%"=="1" (
    for /d %%D in ("%USERPROFILE%\Qt\*") do (
      for %%K in (msvc2022_arm64 msvc2022_64 msvc2019_64) do (
        if exist "%%~fD\%%K\lib\cmake\Qt6\Qt6Config.cmake" set "CMAKE_PREFIX_PATH=%%~fD\%%K\lib\cmake"
      )
    )
  ) else (
    for /d %%D in ("%USERPROFILE%\Qt\*") do (
      for %%K in (mingw_64 msvc2022_arm64 msvc2022_64 msvc2019_64) do (
        if exist "%%~fD\%%K\lib\cmake\Qt6\Qt6Config.cmake" set "CMAKE_PREFIX_PATH=%%~fD\%%K\lib\cmake"
      )
    )
  )
)

if not defined CMAKE_PREFIX_PATH (
  echo Failed to find Qt CMake path.
  echo Set QT_CMAKE_PREFIX_PATH, for example:
  echo   set QT_CMAKE_PREFIX_PATH=%%USERPROFILE%%\Qt\6.11.0\msvc2022_arm64\lib\cmake
  echo   scripts\package_windows.bat
  exit /b 1
)

if "%IS_VS_GENERATOR%"=="1" (
  echo;%CMAKE_PREFIX_PATH% | findstr /I "\\mingw_" >nul
  if not errorlevel 1 (
    echo Error: Visual Studio generator cannot use MinGW Qt: %CMAKE_PREFIX_PATH%
    echo Please set QT_CMAKE_PREFIX_PATH to an msvc kit, e.g.:
    echo   set QT_CMAKE_PREFIX_PATH=%%USERPROFILE%%\Qt\6.11.0\msvc2022_64\lib\cmake
    exit /b 1
  )
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

echo Using Qt CMake path: %CMAKE_PREFIX_PATH%
echo Using CMake generator: %CMAKE_GENERATOR_NAME%
if "%IS_VS_GENERATOR%"=="1" echo Using generator platform: %CMAKE_GENERATOR_PLATFORM_NAME%

set "QT_BIN_DIR="
for %%I in ("%CMAKE_PREFIX_PATH%\..\..") do set "QT_BIN_DIR=%%~fI\bin"
if exist "%QT_BIN_DIR%\Qt6Core.dll" (
  set "PATH=%QT_BIN_DIR%;%PATH%"
  echo Using Qt runtime bin: %QT_BIN_DIR%
) else (
  echo Warning: Qt runtime bin not found from CMAKE_PREFIX_PATH.
  echo   Expected: %QT_BIN_DIR%\Qt6Core.dll
)

set "PKG_DIR=%BUILD_DIR%\packages"
set "STAGE_DIR=%BUILD_DIR%\stage\%BUILD_TYPE%"
set "NSIS_SCRIPT=%ROOT_DIR%\scripts\installer\massiveedit.nsi"
set "NSIS_ICON=%ROOT_DIR%\resources\icons\AppIcon.ico"

if not exist "%PKG_DIR%" mkdir "%PKG_DIR%"
del /q "%PKG_DIR%\MassiveEdit-*.exe" 2>nul
del /q "%PKG_DIR%\MassiveEdit-*.zip" 2>nul

echo [1/6] Configure
if "%IS_VS_GENERATOR%"=="1" (
  cmake -S "%ROOT_DIR%" ^
    -B "%BUILD_DIR%" ^
    -G "%CMAKE_GENERATOR_NAME%" ^
    -A "%CMAKE_GENERATOR_PLATFORM_NAME%" ^
    -DCMAKE_PREFIX_PATH="%CMAKE_PREFIX_PATH%" ^
    -DCMAKE_BUILD_TYPE="%BUILD_TYPE%" ^
    -DMASSIVEEDIT_BUILD_TESTS=ON
) else (
  cmake -S "%ROOT_DIR%" ^
    -B "%BUILD_DIR%" ^
    -G "%CMAKE_GENERATOR_NAME%" ^
    -DCMAKE_PREFIX_PATH="%CMAKE_PREFIX_PATH%" ^
    -DCMAKE_BUILD_TYPE="%BUILD_TYPE%" ^
    -DMASSIVEEDIT_BUILD_TESTS=ON
)
if errorlevel 1 exit /b 1

if defined NUMBER_OF_PROCESSORS (
  set "CORES=%NUMBER_OF_PROCESSORS%"
) else (
  set "CORES=8"
)

echo [2/6] Build (jobs=!CORES!)
cmake --build "%BUILD_DIR%" --clean-first --config "%BUILD_TYPE%" --parallel !CORES!
if errorlevel 1 exit /b 1

if "%RUN_TESTS%"=="1" (
  echo [3/6] Test
  ctest --test-dir "%BUILD_DIR%" --output-on-failure -C "%BUILD_TYPE%"
  if errorlevel 1 exit /b 1
) else (
  echo [3/6] Skip tests (--skip-tests)
)

echo [4/6] Stage install
if exist "%STAGE_DIR%" rmdir /s /q "%STAGE_DIR%"
mkdir "%STAGE_DIR%"
cmake --install "%BUILD_DIR%" --config "%BUILD_TYPE%" --prefix "%STAGE_DIR%"
if errorlevel 1 exit /b 1

set "APP_VERSION=1.0.0"
if exist "%BUILD_DIR%\CMakeCache.txt" (
  for /f "tokens=2 delims==" %%V in ('findstr /B /C:"massiveedit_VERSION:STATIC=" "%BUILD_DIR%\CMakeCache.txt"') do set "APP_VERSION=%%V"
  for /f "tokens=2 delims==" %%V in ('findstr /B /C:"CMAKE_PROJECT_VERSION:STATIC=" "%BUILD_DIR%\CMakeCache.txt"') do set "APP_VERSION=%%V"
)
set "NSIS_OUTPUT=%PKG_DIR%\MassiveEdit-!APP_VERSION!-Setup.exe"

set "NSIS_STATUS=0"
set "NSIS_EXE="
set "NSIS_DIR="

if defined NSIS_ROOT (
  if exist "!NSIS_ROOT!\makensis.exe" set "NSIS_EXE=!NSIS_ROOT!\makensis.exe"
)

if not defined NSIS_EXE (
  where makensis >nul 2>nul
  if not errorlevel 1 set "NSIS_EXE=makensis"
)

if not defined NSIS_EXE (
  if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" set "NSIS_EXE=%ProgramFiles(x86)%\NSIS\makensis.exe"
)

if not defined NSIS_EXE (
  if exist "%ProgramFiles%\NSIS\makensis.exe" set "NSIS_EXE=%ProgramFiles%\NSIS\makensis.exe"
)

echo [5/6] Package NSIS (custom script)
if not exist "%NSIS_SCRIPT%" (
  echo Error: NSIS script not found: %NSIS_SCRIPT%
  exit /b 1
)
if not exist "%NSIS_ICON%" (
  echo Error: NSIS icon not found: %NSIS_ICON%
  exit /b 1
)

if not defined NSIS_EXE (
  set "NSIS_STATUS=127"
  echo Info: makensis not found, skip NSIS.
) else (
  if /I "!NSIS_EXE!"=="makensis" (
    echo Using NSIS from PATH: makensis
  ) else (
    for %%I in ("!NSIS_EXE!") do set "NSIS_DIR=%%~dpI"
    if defined NSIS_DIR set "PATH=!NSIS_DIR!;!PATH!"
    echo Using NSIS: !NSIS_EXE!
  )
  "!NSIS_EXE!" ^
    /INPUTCHARSET UTF8 ^
    "/DAPP_NAME=MassiveEdit" ^
    "/DAPP_VERSION=!APP_VERSION!" ^
    "/DAPP_STAGE=%STAGE_DIR%" ^
    "/DOUT_FILE=%NSIS_OUTPUT%" ^
    "/DAPP_ICON=%NSIS_ICON%" ^
    "%NSIS_SCRIPT%"
  if errorlevel 1 (
    set "NSIS_STATUS=1"
    echo Warning: custom NSIS packaging failed.
  )
)

echo [6/6] Package ZIP
cpack --config "%BUILD_DIR%\CPackConfig.cmake" -C "%BUILD_TYPE%" -G ZIP
if errorlevel 1 exit /b 1

echo [Done] Package complete
echo Packages are in: %PKG_DIR%
dir /b "%PKG_DIR%\MassiveEdit-*"

if "%NSIS_STATUS%"=="1" (
  echo Custom NSIS installer was not generated in this run.
)
if "%NSIS_STATUS%"=="127" (
  echo NSIS is not installed or not discoverable.
)

exit /b 0
