cmake_minimum_required(VERSION 3.28)
project(MyGame LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Pull in the engine (editor not needed for game builds)
set(GAMENGINE_BUILD_EDITOR OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
    gamengine
    GIT_REPOSITORY https://github.com/cmyui/gamengine.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(gamengine)

add_subdirectory(game)
