cmake_minimum_required(VERSION 3.24)
project(NESticleSDL VERSION 0.42.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static MSVC runtime for the application and SDL alike.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(NESTICLE_TEST_HOOKS "Build the 32-bit Windows reference harnesses" OFF)
set(NESTICLE_SDL_SOURCE "" CACHE PATH "SDL3 source tree (fetched when empty)")
set(NESTICLE_SDL_TAG "release-3.4.18" CACHE STRING "SDL3 tag fetched when no source tree is given")

# SDL3, linked statically so a release is one executable plus its data files.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
if(NESTICLE_SDL_SOURCE)
  add_subdirectory("${NESTICLE_SDL_SOURCE}" sdl EXCLUDE_FROM_ALL)
else()
  include(FetchContent)
  FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG "${NESTICLE_SDL_TAG}" GIT_SHALLOW TRUE EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(SDL3)
endif()

file(GLOB NESTICLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(APPEND NESTICLE_SOURCES src/win95/keyb.cpp src/platform/framebuffer.cpp src/platform/sdl3.cpp)
set(NESTICLE_DATA "${CMAKE_CURRENT_SOURCE_DIR}/gui.vol" "${CMAKE_CURRENT_SOURCE_DIR}/NESticle.pal")

# The 1997 sources assume unsigned char, writable string literals and wrapping
# signed arithmetic; keep those semantics on every compiler.
function(nesticle_options target)
  target_include_directories(${target} PRIVATE src src/m6502)
  target_compile_definitions(${target} PRIVATE WIN95 NESTICLE_SDL)
  if(MSVC)
    target_compile_options(${target} PRIVATE /J /utf-8 /permissive /Zc:strictStrings- /EHsc /W3
      /wd4244 /wd4267 /wd4305 /wd4309 /wd4996 /wd4068 /wd4838)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS NOMINMAX)
  else()
    target_compile_options(${target} PRIVATE -funsigned-char -fwrapv -fno-strict-aliasing
      -fno-delete-null-pointer-checks -Wno-narrowing -Wno-write-strings)
  endif()
  if(WIN32)
    target_link_libraries(${target} PRIVATE ws2_32)
    target_sources(${target} PRIVATE src/platform/nesticle.rc)
    if(MINGW)
      target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
  else()
    # The 1997 headers spell out the Win32 calling convention.
    target_compile_definitions(${target} PRIVATE "__cdecl=")
    if(NOT APPLE)
      target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
    endif()
  endif()
  target_link_libraries(${target} PRIVATE SDL3::SDL3-static)
endfunction()

add_executable(nesticle ${NESTICLE_SOURCES})
nesticle_options(nesticle)
if(WIN32)
  set_target_properties(nesticle PROPERTIES WIN32_EXECUTABLE ON)
  if(MSVC)
    target_link_options(nesticle PRIVATE /ENTRY:mainCRTStartup)
  endif()
endif()
if(MSVC)
  # The resource script supplies the manifest.
  target_link_options(nesticle PRIVATE /MANIFEST:NO)
endif()
add_custom_command(TARGET nesticle POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NESTICLE_DATA} "$<TARGET_FILE_DIR:nesticle>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL3_SOURCE_DIR}/LICENSE.txt" "$<TARGET_FILE_DIR:nesticle>/SDL-LICENSE.txt")

# Reference harnesses execute code from the user's own NESTCL95.EXE and need
# 32-bit Windows. They are development tools, never part of a release.
if(NESTICLE_TEST_HOOKS)
  if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "NESTICLE_TEST_HOOKS requires a 32-bit Windows compiler")
  endif()
  add_executable(nesticle-test ${NESTICLE_SOURCES} tools/lockstep.cpp tools/reference_gui.cpp)
  nesticle_options(nesticle-test)
  target_compile_definitions(nesticle-test PRIVATE NESTICLE_TEST_HOOKS)
  target_link_libraries(nesticle-test PRIVATE crypt32)
  add_custom_command(TARGET nesticle-test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NESTICLE_DATA} "$<TARGET_FILE_DIR:nesticle-test>")
  add_executable(reference-cpu-test tools/reference_cpu_test.cpp src/cpu_bridge.cpp src/n6502.cpp
    src/nessound.cpp src/release_video.cpp src/patch.cpp src/disasm.cpp src/net_codec.cpp)
  nesticle_options(reference-cpu-test)
endif()
