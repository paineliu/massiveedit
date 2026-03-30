cmake_minimum_required(VERSION 3.21)

project(massiveedit VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/Zc:__cplusplus)
endif()

option(MASSIVEEDIT_BUILD_TESTS "Build unit tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup(REQUIRES 6.5)

add_subdirectory(src)

if(MASSIVEEDIT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

set(CPACK_PACKAGE_NAME "MassiveEdit")
set(CPACK_PACKAGE_VENDOR "MassiveEdit")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A large-file text editor focused on performance.")
set(CPACK_PACKAGE_FILE_NAME "MassiveEdit-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")

if(APPLE)
  set(CPACK_GENERATOR "DragNDrop;TGZ")
  set(CPACK_DMG_VOLUME_NAME "MassiveEdit-${PROJECT_VERSION}")
elseif(WIN32)
  set(CPACK_GENERATOR "ZIP")
  # Windows package defaults (versionless install directory).
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "MassiveEdit")
  set(CPACK_NSIS_PACKAGE_NAME "MassiveEdit")
  set(CPACK_NSIS_DISPLAY_NAME "MassiveEdit")
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
  # Do not use CPack default start menu entries.
  set(CPACK_PACKAGE_EXECUTABLES)
  # Create a single Start Menu shortcut at root: Start Menu/MassiveEdit.lnk
  set(CPACK_NSIS_CREATE_ICONS_EXTRA
      "CreateShortCut '$SMPROGRAMS\\\\MassiveEdit.lnk' '$INSTDIR\\\\bin\\\\massiveedit.exe'\n")
  set(CPACK_NSIS_DELETE_ICONS_EXTRA
      "Delete '$SMPROGRAMS\\\\MassiveEdit.lnk'\n")
else()
  set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
