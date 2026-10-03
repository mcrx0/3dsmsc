cmake_minimum_required(VERSION 3.20)
project(3dsmsc VERSION 0.5.1 LANGUAGES C CXX)

set(APP_NAME "3dsmsc")
set(APP_VERSION "0.5.1")
set(APP_MAINTAINER "@mcrx0")
set(APP_DESCRIPTION "A lightweight offline music player for the Nintendo 3DS")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_3DS "Build the Nintendo 3DS executable" OFF)
option(BUILD_TESTING "Build host-side tests" ON)

file(GLOB FAAD2_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/faad2/libfaad/*.c"
)
add_library(faad2_decoder STATIC ${FAAD2_SOURCES})
target_include_directories(faad2_decoder
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/faad2/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/faad2/libfaad"
)
target_compile_definitions(faad2_decoder PRIVATE
  HAVE_INTTYPES_H=1
  HAVE_MEMCPY=1
  HAVE_STRING_H=1
  HAVE_STRINGS_H=1
  HAVE_SYS_STAT_H=1
  HAVE_SYS_TYPES_H=1
  HAVE_LRINTF=1
  PACKAGE_VERSION="2.11.3"
)
target_compile_options(faad2_decoder PRIVATE -O2)
# -ffloat-store only matters for x87 precision on x86 hosts; on the ARM11's VFP it forces every
# float temporary through memory and slows AAC decoding.
if(NOT BUILD_3DS)
  target_compile_options(faad2_decoder PRIVATE -ffloat-store)
endif()
target_link_libraries(faad2_decoder PUBLIC m)

add_library(3dsmsc_core STATIC
  src/audio/minimp3_impl.cpp
  src/audio/dr_flac_impl.cpp
  src/audio/mp3_decoder.cpp
  src/audio/flac_decoder.cpp
  src/audio/minimp4_impl.cpp
  src/audio/aac_decoder.cpp
  src/audio/equalizer.cpp
  src/config/settings.cpp
  src/config/settings_file.cpp
  src/library/track.cpp
  src/library/library_index.cpp
  src/library/library_cache.cpp
  src/library/browse_index.cpp
  src/ui/browse_navigator.cpp
  src/ui/equalizer_editor.cpp
  src/ui/help_text.cpp
  src/ui/cover_image.cpp
  src/ui/stb_image_impl.cpp
  src/library/metadata.cpp
  src/library/tag_flac.cpp
  src/library/tag_id3.cpp
  src/library/tag_mp3.cpp
  src/library/tag_mp4.cpp
  src/library/tag_text.cpp
  src/library/artwork.cpp
  src/library/embedded_cover.cpp
  src/library/cover_id3.cpp
  src/library/cover_flac.cpp
  src/library/cover_mp4.cpp
  src/library/controller.cpp
  src/library/folder_browser.cpp
  src/library/folder_list_file.cpp
  src/library/filesystem.cpp
  src/library/scanner.cpp
  src/queue/playback_queue.cpp
  src/queue/queue_file.cpp
  src/playback/playback_controller.cpp
)

target_include_directories(3dsmsc_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minimp3"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minimp4"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dr_libs"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb"
)
# stb_image is third-party code written for C-style warnings levels; it is used as received.
set_source_files_properties(src/ui/stb_image_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")

target_compile_options(3dsmsc_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-fno-exceptions;-fno-rtti>
)
target_link_libraries(3dsmsc_core PUBLIC faad2_decoder)

if(BUILD_TESTING)
  enable_testing()
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/*.cpp")
  add_executable(3dsmsc_tests ${TEST_SOURCES})
  target_link_libraries(3dsmsc_tests PRIVATE 3dsmsc_core)
  # The tests are built from assert(); a Release build must not compile them away.
  target_compile_options(3dsmsc_tests PRIVATE -UNDEBUG)
  target_compile_options(3dsmsc_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
  )
  add_test(NAME 3dsmsc_core_tests COMMAND 3dsmsc_tests)
  set_tests_properties(3dsmsc_core_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

if(BUILD_3DS)
  if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "DEVKITPRO must be set when BUILD_3DS=ON")
  endif()
  if(NOT DEFINED ENV{DEVKITARM})
    message(FATAL_ERROR "DEVKITARM must be set when BUILD_3DS=ON")
  endif()

  find_path(LIBCTRU_INCLUDE_DIR
    NAMES 3ds.h
    HINTS "$ENV{DEVKITPRO}/libctru/include"
    REQUIRED
  )
  find_library(LIBCTRU_LIBRARY
    NAMES ctru
    HINTS "$ENV{DEVKITPRO}/libctru/lib" "$ENV{DEVKITPRO}/libctru/armv6k/lib"
    REQUIRED
  )
  find_library(LIBCTRO3D_LIBRARY
    NAMES citro3d
    HINTS "$ENV{DEVKITPRO}/libctru/lib" "$ENV{DEVKITPRO}/libctru/armv6k/lib"
    REQUIRED
  )
  find_library(LIBCTRO2D_LIBRARY
    NAMES citro2d
    HINTS "$ENV{DEVKITPRO}/libctru/lib" "$ENV{DEVKITPRO}/libctru/armv6k/lib"
    REQUIRED
  )
  find_program(THREEDSXTOOL
    NAMES 3dsxtool
    HINTS "$ENV{DEVKITPRO}/tools/bin" "$ENV{DEVKITARM}/bin"
  )
  find_program(SMDHTOOL
    NAMES smdhtool
    HINTS "$ENV{DEVKITPRO}/tools/bin" "$ENV{DEVKITARM}/bin"
  )

  add_executable(3dsmsc-3ds
    src/3ds/main.cpp
    src/3ds/app.cpp
    src/3ds/app_input.cpp
    src/3ds/app_panels.cpp
    src/3ds/app_library.cpp
    src/3ds/cassette_renderer.cpp
    src/3ds/cover_loader.cpp
    src/3ds/ndsp_player.cpp
  )
  target_include_directories(3dsmsc-3ds PRIVATE "${LIBCTRU_INCLUDE_DIR}")
  # The About screen shows this, so there is one place to bump the version.
  target_compile_definitions(3dsmsc-3ds PRIVATE APP_VERSION="${APP_VERSION}")
  target_link_libraries(3dsmsc-3ds PRIVATE 3dsmsc_core "${LIBCTRO2D_LIBRARY}" "${LIBCTRO3D_LIBRARY}" "${LIBCTRU_LIBRARY}" m)
  target_compile_options(3dsmsc-3ds PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall;-Wextra;-fno-exceptions;-fno-rtti>
  )

  find_program(TEX3DS NAMES tex3ds HINTS "$ENV{DEVKITPRO}/tools/bin")
  find_package(Python3 COMPONENTS Interpreter)

  set(THREEDSX_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/3dsmsc.3dsx")
  set(THREEDSX_SMDH "${CMAKE_CURRENT_BINARY_DIR}/3dsmsc.smdh")
  set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png")
  set(ROMFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/romfs")
  set(ICON_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/icons")
  set(ICON_SHEET "${ROMFS_DIR}/gfx/icons.t3x")
  file(GLOB ICON_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/*.svg"
       "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/order.txt")

  set(THREEDSX_ARGS "")
  set(THREEDSX_DEPENDS 3dsmsc-3ds)
  if(TEX3DS AND Python3_FOUND)
    add_custom_command(
      OUTPUT "${ICON_SHEET}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${ROMFS_DIR}/gfx"
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-icons.py" "${ICON_BUILD_DIR}"
      COMMAND "${TEX3DS}" -i "${ICON_BUILD_DIR}/icons.t3s" -o "${ICON_SHEET}"
      DEPENDS ${ICON_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-icons.py"
      VERBATIM
    )
    add_custom_target(3dsmsc-icons DEPENDS "${ICON_SHEET}")
    list(APPEND THREEDSX_ARGS "--romfs=${ROMFS_DIR}")
    list(APPEND THREEDSX_DEPENDS 3dsmsc-icons)
  else()
    message(WARNING "tex3ds or Python 3 not found; building without icons (text labels only)")
  endif()

  if(THREEDSXTOOL AND SMDHTOOL)
    add_custom_command(
      OUTPUT "${THREEDSX_SMDH}"
      COMMAND "${SMDHTOOL}" --create
              "${APP_NAME}" "${APP_DESCRIPTION}" "${APP_MAINTAINER}"
              "${APP_ICON}" "${THREEDSX_SMDH}"
      DEPENDS "${APP_ICON}"
      VERBATIM
    )
    add_custom_command(
      OUTPUT "${THREEDSX_OUTPUT}"
      COMMAND "${THREEDSXTOOL}" "$<TARGET_FILE:3dsmsc-3ds>" "${THREEDSX_OUTPUT}"
              "--smdh=${THREEDSX_SMDH}" ${THREEDSX_ARGS}
      DEPENDS ${THREEDSX_DEPENDS} "${THREEDSX_SMDH}"
      VERBATIM
    )
  elseif(THREEDSXTOOL)
    add_custom_command(
      OUTPUT "${THREEDSX_OUTPUT}"
      COMMAND "${THREEDSXTOOL}" "$<TARGET_FILE:3dsmsc-3ds>" "${THREEDSX_OUTPUT}" ${THREEDSX_ARGS}
      DEPENDS ${THREEDSX_DEPENDS}
      VERBATIM
    )
    message(WARNING "smdhtool was not found; the 3DS package will not contain app metadata")
  else()
    message(WARNING "3dsxtool was not found; only the ELF target will be generated")
  endif()

  if(THREEDSXTOOL)
    # Copy the finished package to dist/ at the repository root for easy testing.
    set(DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist")
    add_custom_target(3dsmsc-3dsx ALL
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_DIR}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${THREEDSX_OUTPUT}" "${DIST_DIR}/3dsmsc.3dsx"
      DEPENDS "${THREEDSX_OUTPUT}"
    )
  endif()
endif()
