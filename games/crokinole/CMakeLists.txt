# crokinole: the traditional flicking game on a round board (games/crokinole/README.md).
#   crokinole          the game
#   crokinole_auto     CPU seats only, started from the test switches (cart-smoke, the CPU-only gates)
#   crokinole_test     the rules and physics tests, headless (native only; tests/crokinole-rules.mjs runs it)
set(CRK_RULES src/crk.c src/crk_cpu.c src/rules.c)
set(CRK_SRC src/main.c src/game.c src/view.c src/art.c src/sound.c ${CRK_RULES})
bk_add_app(crokinole SOURCES ${CRK_SRC} ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/assets ART ${CMAKE_CURRENT_SOURCE_DIR}/art TITLE "Crokinole"
  DESCRIPTION "Flick turned wooden discs across a lacquered maple board toward the hole in the middle, past eight rubber pegs. Two to four players or two teams, against the computer at three levels or each other, on a pad, by touch or with the mouse." AUTHOR "Luis Montes")
bk_add_app(crokinole_auto SOURCES ${CRK_SRC} ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/assets PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/crokinole.txt TITLE "Crokinole self-playing"
  DESCRIPTION "Crokinole played by the computer on every seat." AUTHOR "Luis Montes")
target_compile_definitions(crokinole_auto PRIVATE CROKINOLE_AUTOPILOT=1)
# the physics is deterministic only without fused multiply-adds (the same bits on every host)
foreach(t crokinole crokinole_auto)
  target_compile_options(${t} PRIVATE -ffp-contract=off)
endforeach()
if(NOT CW_CART)
  add_executable(crokinole_test tools/crk_test.c ${CRK_RULES} ${BOARDKIT_RULES_SOURCES})
  target_include_directories(crokinole_test PRIVATE ${BOARDKIT_DIR}/..)
  target_compile_options(crokinole_test PRIVATE -ffp-contract=off)
  target_link_libraries(crokinole_test PRIVATE cartwheel m)
endif()
