QT_CMAKE_PREFIX_PATH="$(ls -d "$HOME"/Qt/*/macos/lib/cmake | sort -V | tail -n1)"
cmake -S . -B build-xcode -G Xcode \
  -DCMAKE_PREFIX_PATH="$QT_CMAKE_PREFIX_PATH" \
  -DMASSIVEEDIT_BUILD_TESTS=OFF
