cmake_minimum_required(VERSION 3.25)


macro(sanitizers target_name)
    if (${ENABLE_ADDRSAN} MATCHES ON)
        target_compile_options(${target_name} PRIVATE -fsanitize=address)
        target_link_options(${target_name} PRIVATE -fsanitize=address)
    endif ()

    if (${ENABLE_UBSAN} MATCHES ON)
        target_compile_options(${target_name} PRIVATE -fsanitize=undefined)
        target_link_options(${target_name} PRIVATE -fsanitize=undefined)
    endif ()

    if (${ENABLE_TSAN} MATCHES ON)
        target_compile_options(${target_name} PRIVATE -fsanitize=thread)
        target_link_options(${target_name} PRIVATE -fsanitize=thread)
    endif ()
endmacro()

macro(compile_options target_name)
    if (NOT ${MOLD} STREQUAL MOLD-NOTFOUND AND NOT EMSCRIPTEN)
        target_compile_options(${target_name} PUBLIC -fuse-ld=mold)
    endif ()

    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Wno-comment)
    target_link_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Wno-comment)
    sanitizers(${target_name})
endmacro()

macro(blt_add_project name source type)

    project(${name}-${type})

    add_executable(${name}-${type} ${source})

    target_link_libraries(${name}-${type} PRIVATE BLT blt-gp Threads::Threads)

    compile_options(${name}-${type})
    target_compile_definitions(${name}-${type} PRIVATE BLT_DEBUG_LEVEL=${DEBUG_LEVEL})

    if (${TRACK_ALLOCATIONS})
        target_compile_definitions(${name}-${type} PRIVATE BLT_TRACK_ALLOCATIONS=1)
    endif ()

    add_test(NAME ${name} COMMAND ${name}-${type})

    set_property(TEST ${name} PROPERTY FAIL_REGULAR_EXPRESSION "FAIL;ERROR;FATAL;exception")

    project(minecraft-color-picker)
endmacro()

project(minecraft-color-picker VERSION 0.0.1)

option(ENABLE_ADDRSAN "Enable the address sanitizer" OFF)
option(ENABLE_UBSAN "Enable the ub sanitizer" OFF)
option(ENABLE_TSAN "Enable the thread data race sanitizer" OFF)
option(BUILD_MINECRAFT_COLOR_PICKER_EXAMPLES "Build example programs. This will build with CTest" OFF)
option(BUILD_MINECRAFT_COLOR_PICKER_TESTS "Build test programs. This will build with CTest" OFF)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(lib/blt-with-graphics)
add_subdirectory(lib/json)

if (EMSCRIPTEN)
    include(FetchContent)
    FetchContent_Declare(SQLite3
            URL https://www.sqlite.org/2025/sqlite-amalgamation-3500300.zip
            URL_HASH SHA3_256=a49fef837c51bc3375df30c0b0ec148800dce12503289ab94776cc01a675e945
    )
    FetchContent_MakeAvailable(SQLite3)

    set(SQLITE_FILE "${sqlite3_SOURCE_DIR}/shell.c" "${sqlite3_SOURCE_DIR}/sqlite3.c")
    include_directories(${sqlite3_SOURCE_DIR})
else()
    find_package(SQLite3 REQUIRED)
    set (SQLITE_FILE "")
endif ()
#find_package(OpenCV REQUIRED)
include_directories( ${OpenCV_INCLUDE_DIRS} )

include_directories(include/)
file(GLOB_RECURSE PROJECT_BUILD_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(minecraft-color-picker ${PROJECT_BUILD_FILES} ${SQLITE_FILE})

if (EMSCRIPTEN)
    message("Linking Emscripten")
    #    set(BLT_PRELOAD_PATH ../data)
    include(lib/blt-with-graphics/cmake/link_flags.cmake)
else()
    target_link_libraries(minecraft-color-picker PRIVATE SQLite::SQLite3)
endif ()

compile_options(minecraft-color-picker)

target_link_libraries(minecraft-color-picker PRIVATE BLT_WITH_GRAPHICS)
target_link_libraries(minecraft-color-picker PRIVATE nlohmann_json::nlohmann_json ${OpenCV_LIBS})

if (${BUILD_MINECRAFT_COLOR_PICKER_EXAMPLES})

endif()

if (BUILD_MINECRAFT_COLOR_PICKER_TESTS)

endif()
