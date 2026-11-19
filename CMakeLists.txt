cmake_minimum_required(VERSION 3.16)
project(mcu_console_games LANGUAGES C ASM)
add_executable(games
    src/startup.S
    src/platform.c
    src/menu.c
    src/minesweeper/board.c
    src/minesweeper/game.c
)
set_target_properties(games PROPERTIES SUFFIX ".elf" C_STANDARD 11)
target_include_directories(games PRIVATE include)
target_compile_options(games PRIVATE -mcpu=cortex-m4 -mthumb -ffreestanding -Wall -Wextra -Og -g3)
target_link_options(games PRIVATE -mcpu=cortex-m4 -mthumb -nostdlib "-T${CMAKE_CURRENT_SOURCE_DIR}/src/stm32f407.ld")
set_property(TARGET games APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/stm32f407.ld")
target_link_libraries(games PRIVATE gcc)
