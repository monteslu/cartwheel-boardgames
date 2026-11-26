# facet (Facet): a lapidary guild's card market for 2 to 4 players (README.md, the Facet section).
#   facet       the game
#   facet_auto  CPU seats on every seat (Easy, Normal, Hard), match after match: what cart-smoke runs
#   facet_rules_test, facet_cpu_test   headless tests (native only; tests/facet-rules.mjs runs them)
set(FACET_RULES src/rules.c src/cpu.c)
set(FACET_SRC ${FACET_RULES} src/game.c src/cuts.c src/gemshaders.c src/gemtrace.c src/cardgems.c src/main.c)
bk_add_app(facet SOURCES ${FACET_SRC} ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/assets ART art PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/facet.txt TITLE "Facet"
           DESCRIPTION "A lapidary guild market for one to four players on one screen. Gather cut stones from the tray, fulfil commissions whose bonus stones pay for later ones, set cards aside with a topaz and win patrons. First to 15 points ends the round. Computer guilds at Easy, Normal and Hard" AUTHOR "Luis Montes")
bk_add_app(facet_auto SOURCES ${FACET_SRC} ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/assets ART art PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/facet.txt TITLE "Facet Autoplay"
           DESCRIPTION "Facet with computer guilds on every seat" AUTHOR "Luis Montes")
target_compile_definitions(facet_auto PRIVATE FACET_AUTO=1)
foreach(_a facet facet_auto)
  target_include_directories(${_a} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endforeach()
if(NOT CW_CART)
  foreach(_t rules cpu)
    add_executable(facet_${_t}_test tests/${_t}_test.c ${FACET_RULES} ${BOARDKIT_RULES_SOURCES})
    target_include_directories(facet_${_t}_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${BOARDKIT_DIR}/..)
    target_link_libraries(facet_${_t}_test PRIVATE cartwheel m)
  endforeach()
endif()
if(NOT CW_CART)
  add_custom_target(facet_tests) # tests/facet-rules.mjs builds and runs them
  add_dependencies(facet_tests facet_rules_test facet_cpu_test)
endif()
