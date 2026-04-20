set(MASSIVEEDIT_CORE_SOURCES
  core/file/large_file_backend.cpp
  core/file/chunk_cache.cpp
  core/piece_table.cpp
  core/line_indexer.cpp
  core/search_engine.cpp
  core/search_thread_pool.cpp
  core/document_session.cpp
  ${PROJECT_SOURCE_DIR}/include/massiveedit/core/document_session.h
)

add_library(massiveedit_core ${MASSIVEEDIT_CORE_SOURCES})
target_include_directories(massiveedit_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(massiveedit_core PUBLIC Qt6::Core)

set(MASSIVEEDIT_EXE_KIND)
set(MASSIVEEDIT_PLATFORM_SOURCES)
if(WIN32)
  set(MASSIVEEDIT_EXE_KIND WIN32)
  list(APPEND MASSIVEEDIT_PLATFORM_SOURCES
    app_icon.rc
  )
endif()

add_executable(massiveedit ${MASSIVEEDIT_EXE_KIND}
  main.cpp
  resources.qrc
  ${MASSIVEEDIT_PLATFORM_SOURCES}
  ui/main_window.cpp
  ui/large_file_view.cpp
  ui/i18n.cpp
  ${PROJECT_SOURCE_DIR}/include/massiveedit/ui/main_window.h
  ${PROJECT_SOURCE_DIR}/include/massiveedit/ui/large_file_view.h
  ${PROJECT_SOURCE_DIR}/include/massiveedit/ui/i18n.h
)
target_include_directories(massiveedit PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(massiveedit PRIVATE massiveedit_core Qt6::Gui Qt6::Widgets)
target_compile_definitions(massiveedit PRIVATE
  MASSIVEEDIT_VERSION="${PROJECT_VERSION}"
  MASSIVEEDIT_BUILD_CONFIG="$<CONFIG>"
)

if(APPLE)
  set(MASSIVEEDIT_APP_NAME "MassiveEdit")
  set(MASSIVEEDIT_MACOS_ICON "${PROJECT_SOURCE_DIR}/resources/icons/AppIcon.icns")
  set(MASSIVEEDIT_MACOS_INFO_PLIST_IN "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")
  set(MASSIVEEDIT_MACOS_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/MassiveEdit-Info.plist")
  set(ME_EXECUTABLE_NAME "${MASSIVEEDIT_APP_NAME}")
  set(ME_ICON_FILE "AppIcon.icns")
  set(ME_BUNDLE_IDENTIFIER "cn.fivequestions.massiveedit")
  set(ME_BUNDLE_NAME "${MASSIVEEDIT_APP_NAME}")
  set(ME_SHORT_VERSION "${PROJECT_VERSION}")
  set(ME_BUNDLE_VERSION "${PROJECT_VERSION}")
  set(ME_APP_CATEGORY "public.app-category.productivity")
  configure_file("${MASSIVEEDIT_MACOS_INFO_PLIST_IN}" "${MASSIVEEDIT_MACOS_INFO_PLIST}" @ONLY)
  set_source_files_properties("${MASSIVEEDIT_MACOS_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  target_sources(massiveedit PRIVATE "${MASSIVEEDIT_MACOS_ICON}")
  # macOS 26 SDK no longer ships AGL in SDK framework search paths.
  # Qt may still link it transitively, so add system frameworks search path.
  target_link_options(massiveedit PRIVATE "-Wl,-F/System/Library/Frameworks")
  set_target_properties(massiveedit PROPERTIES
    OUTPUT_NAME "${MASSIVEEDIT_APP_NAME}"
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "MassiveEdit"
    MACOSX_BUNDLE_GUI_IDENTIFIER "cn.fivequestions.massiveedit"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
    MACOSX_BUNDLE_INFO_PLIST "${MASSIVEEDIT_MACOS_INFO_PLIST}"
    XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
    XCODE_ATTRIBUTE_ARCHS "arm64"
    XCODE_ATTRIBUTE_EXCLUDED_ARCHS "x86_64"
    XCODE_ATTRIBUTE_EXCLUDED_ARCHS__EFFECTIVE_PLATFORM_SUFFIX_macosx "x86_64"
    XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
    XCODE_ATTRIBUTE_INFOPLIST_FILE "${MASSIVEEDIT_MACOS_INFO_PLIST}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "cn.fivequestions.massiveedit"
    XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "YES"
    XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
    XCODE_ATTRIBUTE_COPY_PHASE_STRIP "NO"
    INSTALL_RPATH "@executable_path/../Frameworks"
  )
endif()

install(TARGETS massiveedit
  BUNDLE DESTINATION .
  RUNTIME DESTINATION bin
)

if(APPLE)
  get_target_property(_qt_qmake_executable Qt6::qmake IMPORTED_LOCATION)
  if(NOT _qt_qmake_executable)
    message(FATAL_ERROR "Qt6::qmake imported location not found.")
  endif()
  get_filename_component(_qt_bin_dir "${_qt_qmake_executable}" DIRECTORY)
  set(_macdeployqt_executable "${_qt_bin_dir}/macdeployqt")
  install(CODE "
    set(_app \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${MASSIVEEDIT_APP_NAME}.app\")
    execute_process(
      COMMAND \"${_macdeployqt_executable}\" \"\${_app}\" -always-overwrite -no-strip
      RESULT_VARIABLE _deploy_result
      OUTPUT_VARIABLE _deploy_out
      ERROR_VARIABLE _deploy_err
    )
    if(NOT _deploy_result EQUAL 0)
      message(FATAL_ERROR \"macdeployqt failed with code \${_deploy_result}\\n\${_deploy_out}\\n\${_deploy_err}\")
    endif()
  ")
endif()

if(WIN32)
  get_target_property(_qt_qmake_executable Qt6::qmake IMPORTED_LOCATION)
  if(NOT _qt_qmake_executable)
    message(FATAL_ERROR "Qt6::qmake imported location not found.")
  endif()
  get_filename_component(_qt_bin_dir "${_qt_qmake_executable}" DIRECTORY)
  set(_windeployqt_executable "${_qt_bin_dir}/windeployqt.exe")
  install(CODE "
    set(_app \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/massiveedit.exe\")
    execute_process(
      COMMAND \"${_windeployqt_executable}\" --no-compiler-runtime --no-translations \"\${_app}\"
      RESULT_VARIABLE _deploy_result
      OUTPUT_VARIABLE _deploy_out
      ERROR_VARIABLE _deploy_err
    )
    if(NOT _deploy_result EQUAL 0)
      message(FATAL_ERROR \"windeployqt failed with code \${_deploy_result}\\n\${_deploy_out}\\n\${_deploy_err}\")
    endif()
  ")
endif()
