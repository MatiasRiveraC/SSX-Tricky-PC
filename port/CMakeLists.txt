cmake_minimum_required(VERSION 3.20)

# ============================================================================
# SSX Tricky (Xbox) -- native PC port
#
#   cmake -S port -B port/build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
#   cmake --build port/build -j
#
# The translated game code (src/recomp/gen/, src/recomp/recomp_funcs.h) is not
# in the repository: it is generated from your own copy of the game. See
# docs/building.md.
# ============================================================================

project(ssx_tricky C)

# The launcher's icon and manifest (src/ssx_recomp.rc) need the resource
# compiler (windres with MinGW, rc.exe with MSVC).
enable_language(RC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# ── xboxrecomp runtime (vendored at the repository root) ───────────────────
set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../xboxrecomp" CACHE PATH
    "Path to the xboxrecomp toolkit root directory")
add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

# ── Translated game code ───────────────────────────────────────────────────
# CONFIGURE_DEPENDS so regenerating picks up added or removed files without a
# manual re-configure.
file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "src/recomp/gen/*.c")
if(NOT RECOMP_GEN_SOURCES)
    message(FATAL_ERROR
        "port/src/recomp/gen/ is empty: the translated game code has not been "
        "generated yet. It is made from your own copy of the game and is never "
        "stored in this repository -- see docs/building.md.")
endif()

set(GAME_SOURCES
    src/main.c
    src/launcher.c
    src/controls.c
    src/hostui.c
    src/ssx_recomp.rc
    src/recomp_manual.c
    src/xapi_input_hle.c
    src/recomp/recomp_probe.c
)

add_executable(${PROJECT_NAME} WIN32
    ${GAME_SOURCES}
    ${RECOMP_GEN_SOURCES}
)

# Name the executable after the title in the XBE certificate ("SSX Tricky"),
# falling back to that name when no XBE is present.
include(${XBOXRECOMP_DIR}/cmake/XbeTitleName.cmake)
xbe_title_name(GAME_TITLE "${CMAKE_CURRENT_SOURCE_DIR}/../game_files/default.xbe"
               FALLBACK "SSX Tricky")
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${GAME_TITLE}")
message(STATUS "Executable will be named: ${GAME_TITLE}.exe")

target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/recomp
    ${XBOXRECOMP_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d11
    dxgi
    dxguid
    $<IF:$<BOOL:${MINGW}>,xinput1_4,xinput>   # XInput 1.4 ships with Windows
    winmm
    # Launcher and menu bar: themed controls, file/folder pickers, shell
    comctl32
    ole32
    shell32
    uuid
    d3dcompiler
)

if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE /LARGEADDRESSAWARE)
    # The generated files are very large.
    target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
endif()
