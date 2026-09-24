cmake_minimum_required(VERSION 3.20)
project(3dsmsc VERSION 0.4.2 LANGUAGES C CXX)

set(APP_NAME "3dsmsc")
set(APP_VERSION "0.4.2b")
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
list(FILTER FAAD2_SOURCES EXCLUDE REGEX "/(drm_dec|ssr|ssr_fb|ssr_ipqf)\\.c$")
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
target_compile_options(faad2_decoder PRIVATE -O2 -ffloat-store)
target_link_libraries(faad2_decoder PUBLIC m)

add_library(3dsmsc_core STATIC
  src/audio/minimp3_impl.cpp
  src/audio/dr_flac_impl.cpp
  src/audio/mp3_decoder.cpp
  src/audio/flac_decoder.cpp
  src/audio/minimp4_impl.cpp
  src/audio/aac_decoder.cpp
  src/config/settings.cpp
  src/config/settings_file.cpp
  src/library/track.cpp
  src/library/library_index.cpp
  src/library/metadata.cpp
  src/library/artwork.cpp
  src/library/controller.cpp
  src/library/folder_browser.cpp
  src/library/filesystem.cpp
  src/library/scanner.cpp
  src/queue/playback_queue.cpp
  src/queue/queue_file.cpp
)

target_include_directories(3dsmsc_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minimp3"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minimp4"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dr_libs"
)

target_compile_options(3dsmsc_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-fno-exceptions;-fno-rtti>
)
target_link_libraries(3dsmsc_core PUBLIC faad2_decoder)

if(BUILD_TESTING)
  enable_testing()
  add_executable(3dsmsc_tests tests/unit/core_tests.cpp)
  target_link_libraries(3dsmsc_tests PRIVATE 3dsmsc_core)
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
    src/3ds/cassette_renderer.cpp
    src/3ds/ndsp_player.cpp
  )
  target_include_directories(3dsmsc-3ds PRIVATE "${LIBCTRU_INCLUDE_DIR}")
  target_link_libraries(3dsmsc-3ds PRIVATE 3dsmsc_core "${LIBCTRO2D_LIBRARY}" "${LIBCTRO3D_LIBRARY}" "${LIBCTRU_LIBRARY}" m)
  target_compile_options(3dsmsc-3ds PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall;-Wextra;-fno-exceptions;-fno-rtti>
  )

  set(THREEDSX_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/3dsmsc.3dsx")
  set(THREEDSX_SMDH "${CMAKE_CURRENT_BINARY_DIR}/3dsmsc.smdh")
  set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png")
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
              "--smdh=${THREEDSX_SMDH}"
      DEPENDS 3dsmsc-3ds "${THREEDSX_SMDH}"
      VERBATIM
    )
    add_custom_target(3dsmsc-3dsx ALL DEPENDS "${THREEDSX_OUTPUT}")
  elseif(THREEDSXTOOL)
    add_custom_command(
      OUTPUT "${THREEDSX_OUTPUT}"
      COMMAND "${THREEDSXTOOL}" "$<TARGET_FILE:3dsmsc-3ds>" "${THREEDSX_OUTPUT}"
      DEPENDS 3dsmsc-3ds
      VERBATIM
    )
    add_custom_target(3dsmsc-3dsx ALL DEPENDS "${THREEDSX_OUTPUT}")
    message(WARNING "smdhtool was not found; the 3DS package will not contain app metadata")
  else()
    message(WARNING "3dsxtool was not found; only the ELF target will be generated")
  endif()
endif()
