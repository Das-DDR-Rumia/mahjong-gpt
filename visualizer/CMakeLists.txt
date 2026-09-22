cmake_minimum_required(VERSION 3.20)
project(mahjong_gpt_visualizer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(mahjong_viz SHARED src/mahjong_viz.c)
set_target_properties(mahjong_viz PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_include_directories(mahjong_viz PUBLIC include)
target_compile_definitions(mahjong_viz PRIVATE UNICODE _UNICODE)
target_link_libraries(mahjong_viz PUBLIC gdi32 user32 bcrypt)

add_executable(mahjong_viz_demo demo/demo.c)
target_link_libraries(mahjong_viz_demo PRIVATE mahjong_viz)

enable_testing()
add_executable(mahjong_viz_tests tests/test_mahjong_viz.c)
target_link_libraries(mahjong_viz_tests PRIVATE mahjong_viz)
target_compile_definitions(
    mahjong_viz_tests PRIVATE
    MV_ARTIFACT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/artifacts"
)
add_test(NAME mahjong_viz_tests COMMAND mahjong_viz_tests)
