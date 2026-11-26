# boardkit: the shared board-game kit (docs/boardkit.md). Included by the top CMakeLists.txt.
#
#   bk_add_app(<name> SOURCES ... [any cw_add_app argument])
#
# is cw_add_app plus the kit's sources and include path: a game includes "boardkit/bk_game.h" and so on.
# BOARDKIT_RULES_SOURCES is the part with no rendering (rules interface, search, turns, deck, bag, dice
# rules): headless tests and a game's own soak tools link it with cartwheel's core alone.
set(BOARDKIT_DIR ${CMAKE_CURRENT_LIST_DIR})
set(BOARDKIT_RULES_SOURCES
  ${BOARDKIT_DIR}/bk_game.c
  ${BOARDKIT_DIR}/bk_match.c
  ${BOARDKIT_DIR}/bk_pieces.c
  ${BOARDKIT_DIR}/bk_cpu.c)
set(BOARDKIT_VIEW_SOURCES
  ${BOARDKIT_DIR}/bk_view.c
  ${BOARDKIT_DIR}/bk_card.c
  ${BOARDKIT_DIR}/bk_select.c
  ${BOARDKIT_DIR}/bk_shell.c)
set(BOARDKIT_SOURCES ${BOARDKIT_RULES_SOURCES} ${BOARDKIT_VIEW_SOURCES})

function(bk_add_app name)
  cw_add_app(${name} ${ARGN})
  target_sources(${name} PRIVATE ${BOARDKIT_SOURCES})
  target_include_directories(${name} PRIVATE ${BOARDKIT_DIR}/..)
endfunction()

# the kit's own headless tests (native only): tests/boardkit.mjs runs them, each with its control
if(NOT CW_CART)
  file(GLOB _bk_tests CONFIGURE_DEPENDS ${BOARDKIT_DIR}/tests/test_*.c)
  foreach(_t ${_bk_tests})
    get_filename_component(_n ${_t} NAME_WE)
    add_executable(bk_${_n} ${_t} ${BOARDKIT_SOURCES})
    target_include_directories(bk_${_n} PRIVATE ${BOARDKIT_DIR}/..)
    target_link_libraries(bk_${_n} PRIVATE cartwheel m)
  endforeach()
endif()
