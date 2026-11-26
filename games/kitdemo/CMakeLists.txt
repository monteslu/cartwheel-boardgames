# kitdemo: the board-game kit's reference game (docs/boardkit.md); game agents copy its layout.
#   kitdemo        the game
#   kitdemo_auto   two CPUs play match after match (cart-smoke runs it)
set(KD_SRC src/rules.c src/game.c src/main.c)
set(KD_ART TITLE "Kit Demo" DESCRIPTION "The reference game for the board-game kit." AUTHOR "Luis Montes")
bk_add_app(kitdemo SOURCES ${KD_SRC} ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/assets ${KD_ART})
bk_add_app(kitdemo_auto SOURCES ${KD_SRC} ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/assets PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/kitdemo.txt ${KD_ART})
target_compile_definitions(kitdemo_auto PRIVATE KITDEMO_AUTO=1)
