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
    if (NOT ${MOLD} STREQUAL MOLD-NOTFOUND)
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

find_package(SQLite3 REQUIRED)

include_directories(include/)
file(GLOB_RECURSE PROJECT_BUILD_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(minecraft-color-picker ${PROJECT_BUILD_FILES})

compile_options(minecraft-color-picker)

target_link_libraries(minecraft-color-picker PRIVATE BLT_WITH_GRAPHICS SQLite::SQLite3)
target_link_libraries(minecraft-color-picker PRIVATE nlohmann_json::nlohmann_json)

if (${BUILD_MINECRAFT_COLOR_PICKER_EXAMPLES})

endif()

if (BUILD_MINECRAFT_COLOR_PICKER_TESTS)

endif()
