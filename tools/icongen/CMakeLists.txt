find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Svg)

add_executable(elv_icongen main.cpp)
set_target_properties(elv_icongen PROPERTIES OUTPUT_NAME elvideo-icongen)
target_link_libraries(elv_icongen PRIVATE elv_options Qt6::Gui Qt6::Svg)
