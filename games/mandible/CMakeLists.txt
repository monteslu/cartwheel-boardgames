# mandible: Mandible, a two-player game of carved insect pieces on an open table (README "Mandible").
#   mandible        the game
#   mandible_auto   the self-playing cart cart-smoke runs (CPU Hard against CPU Normal, rematches by itself)
#   mandible_test_rules   the rules tests (native only; tests/mandible-rules.mjs runs them)
set(SW_SRC src/main.c src/match.c src/rules.c src/art.c)
set(SW_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/assets)
bk_add_app(mandible SOURCES ${SW_SRC} ASSETS ${SW_ASSETS} ART ${CMAKE_CURRENT_SOURCE_DIR}/art TITLE "Mandible" DESCRIPTION "Surround the other Empress with carved insect pieces on an open table." AUTHOR "Luis Montes")
bk_add_app(mandible_auto SOURCES ${SW_SRC} ASSETS ${SW_ASSETS} PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/mandible.txt TITLE "Mandible Self-Playing" DESCRIPTION "Mandible played by two CPU seats." AUTHOR "Luis Montes")
target_compile_definitions(mandible_auto PRIVATE MANDIBLE_AUTO=1)
foreach(t mandible mandible_auto)
  target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endforeach()

if(NOT CW_CART)
  add_executable(mandible_test_rules tests/test_rules.c src/rules.c ${BOARDKIT_RULES_SOURCES})
  target_include_directories(mandible_test_rules PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${BOARDKIT_DIR}/..)
  target_link_libraries(mandible_test_rules PRIVATE cartwheel m)
endif()
