find_package(CLI11 CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(ftxui CONFIG REQUIRED)

add_executable(vdbg main.cpp tui.cpp)
target_link_libraries(vdbg PRIVATE vlinux)
target_link_libraries(
    vdbg
    PRIVATE
    CLI11::CLI11 fmt::fmt outcome::hl
    ftxui::dom ftxui::ftxui ftxui::screen ftxui::component
)
target_include_directories(vdbg PRIVATE ${CMAKE_SOURCE_DIR}/include)
