# Shared library loaded by editor at runtime.
# Symbols resolve from the host process at load time.
file(GLOB SCRIPT_SOURCES CONFIGURE_DEPENDS src/scripts/*.cpp)
add_library(GameScripts SHARED ${SCRIPT_SOURCES})
target_include_directories(GameScripts PRIVATE src/)
# Link Engine headers + public deps (glm, entt, spdlog, etc.) but not Engine itself
target_link_libraries(GameScripts PRIVATE Engine compiler_warnings)
set_target_properties(GameScripts PROPERTIES LINK_FLAGS "")

if(APPLE)
    target_link_options(GameScripts PRIVATE -undefined dynamic_lookup)
elseif(UNIX)
    target_link_options(GameScripts PRIVATE -Wl,--unresolved-symbols=ignore-in-shared-libs)
endif()

# Standalone executable
add_executable(Game
    src/main.cpp
    src/game_application.cpp
    ${SCRIPT_SOURCES}
)
target_link_libraries(Game PRIVATE Engine compiler_warnings)
target_include_directories(Game PRIVATE src/)
