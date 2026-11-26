# Board games on cartwheel-engine. Built as in the other cartwheel example repos (scripts/build.sh): against a
# prebuilt engine SDK, or with the engine from source (CARTWHEEL_ENGINE=<checkout> scripts/fetch-deps.sh).
#
# Every games/<name>/CMakeLists.txt is picked up here. A game declares its apps with bk_add_app (cw_add_app plus
# the shared board-game kit, games/common/boardkit; see docs/boardkit.md).
if(NOT COMMAND cw_add_app)
  cmake_minimum_required(VERSION 3.20)
  project(cartwheel_boardgames C CXX)
  find_package(cartwheel REQUIRED CONFIG)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/games/common/boardkit/boardkit.cmake)

file(GLOB game_lists CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/games/*/CMakeLists.txt)
foreach(list ${game_lists})
  get_filename_component(dir ${list} DIRECTORY)
  add_subdirectory(${dir})
endforeach()
