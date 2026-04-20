cmake_minimum_required(VERSION 3.21)

project(massiveedit VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/Zc:__cplusplus)
endif()

option(MASSIVEEDIT_BUILD_TESTS "Build unit tests" ON)

if(APPLE AND CMAKE_GENERATOR STREQUAL "Xcode")
  if(NOT DEFINED CMAKE_OSX_SYSROOT OR CMAKE_OSX_SYSROOT STREQUAL "")
    # Qt's Xcode setup macro expects a non-empty SDK name.
    set(CMAKE_OSX_SYSROOT "macosx" CACHE STRING "macOS SDK" FORCE)
  endif()
  # Ensure App Store archives always include dSYM symbols.
  set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
      CACHE STRING "Xcode debug info format" FORCE)
  set(CMAKE_XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "YES"
      CACHE STRING "Xcode generate debug symbols" FORCE)
  set(CMAKE_XCODE_ATTRIBUTE_COPY_PHASE_STRIP "NO"
      CACHE STRING "Xcode strip in copy phase" FORCE)
endif()

if(APPLE)
  # Use a deterministic arch for macOS builds so Qt (Homebrew arm64) and app arch match.
  # Override with -DCMAKE_OSX_ARCHITECTURES=... if needed.
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS architectures" FORCE)
endif()

if(APPLE)
  # New macOS SDKs may not provide AGL stubs, but Qt's WrapOpenGL still tries to link AGL.
  # Point this probe to OpenGL framework to avoid injecting "-framework AGL".
  if(NOT DEFINED WrapOpenGL_AGL OR WrapOpenGL_AGL STREQUAL ""
     OR WrapOpenGL_AGL MATCHES "AGL\\.framework/?$")
    set(WrapOpenGL_AGL "/System/Library/Frameworks/OpenGL.framework"
        CACHE FILEPATH "Compatibility override for Qt WrapOpenGL on modern macOS SDKs" FORCE)
  endif()
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup(REQUIRES 6.5)

add_subdirectory(src)

if(MASSIVEEDIT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

set(CPACK_PACKAGE_NAME "MassiveEdit")
set(CPACK_PACKAGE_VENDOR "FiveQuestions")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A large-file text editor focused on performance.")
set(CPACK_PACKAGE_FILE_NAME "MassiveEdit-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")

if(APPLE)
  set(CPACK_GENERATOR "DragNDrop;TGZ")
  set(CPACK_DMG_VOLUME_NAME "MassiveEdit-${PROJECT_VERSION}")
elseif(WIN32)
  set(CPACK_GENERATOR "ZIP")
  # Keep install directory versionless for Windows.
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "MassiveEdit")
  # NSIS installer is built by scripts/package_windows.bat with:
  #   scripts/installer/massiveedit.nsi
else()
  set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
